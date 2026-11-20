// The rendering architecture (D-81): transient textures pooled from frame
// to frame; a frame's passes checked (a transient read before it is written
// is named), planned (work that reaches no output is left out, the order
// kept) and recorded, each pass in a trace span — against a device that only
// remembers what it made. Desktop device pixels are checked in render/cts.
import std;
import xrender.graph;
import mlibs.rhi;
import mlibs.memory;
import mlibs.trace;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::SlotMap;
}

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

// Remembers the textures it made; runs nothing.
class Textures final : public mlibs::rhi::Device {
public:
    mlibs::rhi::Caps caps_{.backend = mlibs::rhi::Backend::vulkan, .device = "textures", .formats = mlibs::rhi::formats({mlibs::rhi::ShaderFormat::spirv})};
    std::size_t destroyed = 0;

    const mlibs::rhi::Caps& caps() const noexcept override { return caps_; }
    void destroy(mlibs::rhi::Buffer) noexcept override {}
    void destroy(mlibs::rhi::Texture t) noexcept override {
        if (textures_.get(t)) ++destroyed;
        textures_.erase(t);
    }
    void destroy(mlibs::rhi::Sampler) noexcept override {}
    void destroy(mlibs::rhi::GraphicsPipeline) noexcept override {}
    void destroy(mlibs::rhi::ComputePipeline) noexcept override {}
    std::optional<mlibs::rhi::BufferDesc> describe(mlibs::rhi::Buffer) const noexcept override { return std::nullopt; }
    std::optional<mlibs::rhi::TextureDesc> describe(mlibs::rhi::Texture t) const noexcept override {
        auto* d = textures_.get(t);
        return d ? std::optional(*d) : std::nullopt;
    }
    bool exists(mlibs::rhi::Sampler) const noexcept override { return false; }
    std::optional<mlibs::rhi::GraphicsPipelineInfo> describe(mlibs::rhi::GraphicsPipeline) const noexcept override { return std::nullopt; }
    std::optional<mlibs::rhi::ComputePipelineInfo> describe(mlibs::rhi::ComputePipeline) const noexcept override { return std::nullopt; }
    mlibs::rhi::Result<void> wait(mlibs::rhi::Submission) override { return {}; }
    mlibs::rhi::Result<void> wait_idle() override { return {}; }
    [[nodiscard]] std::size_t live() const noexcept { return textures_.size(); }

protected:
    mlibs::rhi::Result<mlibs::rhi::Buffer> make(const mlibs::rhi::BufferDesc&) override { return mlibs::rhi::fail(mlibs::rhi::Errc::unsupported, "textures only"); }
    mlibs::rhi::Result<mlibs::rhi::Texture> make(const mlibs::rhi::TextureDesc& d) override { return textures_.insert({d.format, d.width, d.height, d.usage, {}, d.samples, d.levels}); }
    mlibs::rhi::Result<mlibs::rhi::Sampler> make(const mlibs::rhi::SamplerDesc&) override { return mlibs::rhi::fail(mlibs::rhi::Errc::unsupported, "textures only"); }
    mlibs::rhi::Result<mlibs::rhi::GraphicsPipeline> make(const mlibs::rhi::GraphicsPipelineDesc&) override { return mlibs::rhi::fail(mlibs::rhi::Errc::unsupported, "textures only"); }
    mlibs::rhi::Result<mlibs::rhi::ComputePipeline> make(const mlibs::rhi::ComputePipelineDesc&) override { return mlibs::rhi::fail(mlibs::rhi::Errc::unsupported, "textures only"); }
    mlibs::rhi::Result<mlibs::rhi::Submission> run(const mlibs::rhi::CommandList&) override { return mlibs::rhi::Submission{1}; }
    mlibs::rhi::Result<void> fetch(mlibs::rhi::Texture, mlibs::rhi::Region, std::span<std::byte>) override { return {}; }
    mlibs::rhi::Result<void> fetch(mlibs::rhi::Buffer, std::uint32_t, std::span<std::byte>) override { return {}; }

private:
    SlotMap<mlibs::rhi::TextureDesc, mlibs::rhi::TextureTag> textures_;
};

constexpr auto target_usage = mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled;


}  // namespace

UTest cases{"render.graph", [] {

    "the pool hands a texture out again in later frames, and lets go of one nobody asks for"_test = [] {
        Textures device;
        xrender::TexturePool pool(device, 2);
        const xrender::TextureNeed layer{64, 32, mlibs::rhi::Format::rgba8_unorm, target_usage};
        pool.begin_frame();
        auto a = pool.acquire(layer, "a");
        auto b = pool.acquire(layer, "b");
        expect(a && b && *a != *b) << "two of the same need in one frame are two textures";
        pool.begin_frame();
        auto again = pool.acquire(layer, "a");
        expect(again && (*again == *a || *again == *b)) << "the next frame gets one of them back";
        expect(eq(pool.made(), std::size_t{2}));
        // The frame encoded again from the start: what it was handed is free.
        pool.restart_frame();
        auto restarted = pool.acquire(layer, "a");
        expect(restarted && pool.made() == 2) << "a frame started over makes nothing new";
        auto other = pool.acquire({64, 32, mlibs::rhi::Format::r8_unorm, mlibs::rhi::TextureUsage::sampled}, "mask");
        expect(other && pool.made() == 3) << "another format is another texture";
        // Frames without asking: after `keep` of them they go.
        for (int i = 0; i < 4; ++i) pool.begin_frame();
        expect(eq(pool.held(), std::size_t{0}));
        expect(eq(device.live(), std::size_t{0}));
        expect(pool.released() == 3) << std::format("{} released", pool.released());
    };

    "a history is kept by name from frame to frame: what one frame wrote the next reads, and says how long ago; a frame started over has the same two; one nobody asks for goes"_test = [] {
        Textures device;
        xrender::TexturePool pool(device, 2);
        const xrender::TextureNeed light{64, 32, mlibs::rhi::Format::rgba16_float, target_usage};
        pool.begin_frame();
        const auto first = pool.history("gathered", light, "gathered");
        expect(fatal(first.has_value()));
        expect(first->now != first->before && eq(first->past, 0u)) << "new: it holds no past";
        expect(pool.owns(first->now) && pool.owns(first->before));
        const auto same = pool.history("gathered", light, "gathered");
        expect(same && same->now == first->now && same->before == first->before && eq(same->past, 0u)) << "asked twice in a frame: the same two";
        const auto scratch = pool.acquire(light, "scratch");
        expect(scratch && *scratch != first->now && *scratch != first->before) << "nobody else is handed a history's";
        pool.begin_frame();
        const auto second = pool.history("gathered", light, "gathered");
        expect(fatal(second.has_value()));
        expect(second->before == first->now && second->now == first->before) << "what was written is now the past";
        expect(eq(second->past, 1u));
        pool.restart_frame();
        const auto restarted = pool.history("gathered", light, "gathered");
        expect(restarted && restarted->now == second->now && restarted->before == second->before && eq(restarted->past, 1u)) << "a frame started over";
        // A frame that did not ask, then one that does: the past is two frames old, and says so.
        pool.begin_frame();
        pool.begin_frame();
        const auto later = pool.history("gathered", light, "gathered");
        expect(later && later->before == second->now && eq(later->past, 2u));
        // Told to forget: the same two, with no past.
        pool.forget("gathered");
        pool.begin_frame();
        const auto forgotten = pool.history("gathered", light, "gathered");
        expect(forgotten && eq(forgotten->past, 0u) && eq(pool.made(), std::size_t{3})) << "nothing new was made for it";
        pool.begin_frame();
        expect(eq(pool.history("gathered", light, "gathered")->past, 1u));
        // Another size (the window changed): two new ones, the old two destroyed, no past.
        const std::size_t destroyed = device.destroyed;
        pool.begin_frame();
        const auto resized = pool.history("gathered", {128, 64, mlibs::rhi::Format::rgba16_float, target_usage}, "gathered");
        expect(resized && eq(resized->past, 0u) && eq(device.destroyed, destroyed + 2) && eq(pool.histories(), std::size_t{1}));
        // Two names are two histories.
        const auto other = pool.history("other", light, "other");
        expect(other && other->now != resized->now && eq(pool.histories(), std::size_t{2}));
        for (int frame = 0; frame < 4; ++frame) pool.begin_frame();
        expect(eq(pool.histories(), std::size_t{0}) && eq(device.live(), std::size_t{0})) << "nobody asked for `keep` frames";
    };

    "in a graph: the pass that writes a history runs though nothing of this frame reads it; reading this frame's before it is written, or a past that holds none, is named"_test = [] {
        Textures device;
        xrender::TexturePool pool(device);
        const xrender::TextureNeed light{64, 32, mlibs::rhi::Format::rgba16_float, target_usage};
        const auto target = device.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 64, 32, target_usage, "target"});
        expect(fatal(target.has_value()));
        std::vector<std::string> ran;
        const auto frame = [&](bool read_past, bool resolve_first) {
            pool.begin_frame();
            xrender::Graph graph(pool);
            const auto drawn = graph.transient(light, "drawn");
            const auto gathered = graph.history("gathered", light, "gathered");
            expect(fatal(drawn.has_value() && gathered.has_value()));
            ran.clear();
            const auto show = [&] { graph.pass({.name = "show", .reads = {gathered->now}, .writes = {*target}}, [&](mlibs::rhi::CommandList&) { ran.push_back("show"); }); };
            if (resolve_first) show();
            graph.pass({.name = "draw", .writes = {*drawn}}, [&](mlibs::rhi::CommandList&) { ran.push_back("draw"); });
            std::vector<mlibs::rhi::Texture> reads{*drawn};
            if (read_past) reads.push_back(gathered->before);
            graph.pass({.name = "resolve", .reads = reads, .writes = {gathered->now}}, [&](mlibs::rhi::CommandList&) { ran.push_back("resolve"); });
            mlibs::rhi::CommandList list;
            // Nothing of this frame is asked for: no output.
            return std::pair(graph.record(list, {}), gathered->past);
        };
        const auto [first, first_past] = frame(false, false);
        expect(fatal(first.has_value())) << (first ? "" : first.error().front().text);
        expect(eq(first_past, 0u) && eq(first->histories, std::size_t{1}));
        expect(ran == std::vector<std::string>{"draw", "resolve"}) << "kept for the next frame: it runs, and what it reads";
        const auto [second, second_past] = frame(true, false);
        expect(second.has_value() && eq(second_past, 1u)) << "the next frame reads the past";
        pool.forget("gathered");
        const auto [none, none_past] = frame(true, false);
        expect(fatal(!none.has_value()));
        expect(eq(none_past, 0u) && none.error().front().text == "resolve reads the past of `gathered`, which holds none") << none.error().front().text;
        const auto [early, early_past] = frame(true, true);
        expect(fatal(!early.has_value()));
        expect(early.error().front().text == "show reads this frame's `gathered` before any pass writes it") << early.error().front().text;
        device.destroy(*target);
    };

    "a pass that reads a transient texture before any pass writes it is named"_test = [] {
        Textures device;
        xrender::TexturePool pool(device);
        xrender::Graph g(pool);
        auto target = device.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 16, 16, target_usage, "target"});
        auto layer = g.transient({16, 16, mlibs::rhi::Format::rgba8_unorm, target_usage}, "the layer");
        expect(target && layer && g.transient(*layer) && !g.transient(*target));
        g.pass({.name = "ui.composite", .label = "node 7", .reads = {*layer}, .writes = {*target}}, {});
        g.pass({.name = "ui.layer", .writes = {*layer}}, {});
        auto plan = g.plan(std::span(&*target, 1));
        expect(!plan) << "not planned";
        if (!plan) {
            expect(eq(plan.error().size(), std::size_t{1}));
            expect(eq(plan.error()[0].pass, std::size_t{0}));
            expect(eq(plan.error()[0].text, std::string("ui.composite (node 7) reads `the layer` before any pass writes it")));
        }
        // A texture from outside the graph is the frame's to read.
        xrender::Graph h(pool);
        h.pass({.name = "ui.copy", .reads = {*target}, .writes = {*target}}, {});
        expect(h.plan(std::span(&*target, 1)).has_value()) << "the target holds what it held";
    };

    "work that reaches no output is left out; the order is the description's"_test = [] {
        Textures device;
        xrender::TexturePool pool(device);
        xrender::Graph g(pool);
        auto target = device.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 32, 32, target_usage, "target"});
        auto atlas = device.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, 32, 32, target_usage, "atlas"});
        const xrender::TextureNeed need{32, 32, mlibs::rhi::Format::rgba8_unorm, target_usage};
        auto used = g.transient(need, "used layer");
        auto unused = g.transient(need, "a layer nobody composites");
        auto only = g.transient(need, "read only by the unused");
        std::vector<std::string> ran;
        auto log = [&](std::string name) { return [&ran, name](mlibs::rhi::CommandList&) { ran.push_back(name); }; };
        g.pass({.name = "clear", .writes = {*target}}, log("clear"));
        g.pass({.name = "only", .writes = {*only}}, log("only"));
        g.pass({.name = "unused", .reads = {*only}, .writes = {*unused}}, log("unused"));
        g.pass({.name = "used", .writes = {*used}}, log("used"));
        g.pass({.name = "atlas", .writes = {*atlas}}, log("atlas"));  // outside the graph: kept
        g.pass({.name = "composite", .reads = {*used}, .writes = {*target}}, log("composite"));
        auto plan = g.plan(std::span(&*target, 1));
        expect(plan.has_value());
        if (plan) {
            expect(plan->run == std::vector<std::size_t>{0, 3, 4, 5}) << std::format("ran {}", plan->run);
            expect(plan->culled == std::vector<std::size_t>{1, 2}) << std::format("culled {}", plan->culled);
        }
        mlibs::rhi::CommandList list;
        auto report = g.record(list, std::span(&*target, 1));
        expect(report && report->recorded == 4 && report->culled == 2 && report->transients == 3);
        expect(ran == std::vector<std::string>{"clear", "used", "atlas", "composite"}) << std::format("{}", ran);
        g.clear();
        expect(g.size() == 0 && !g.transient(*used)) << "cleared for the next frame";
    };

    "each pass that runs is a trace span of its name"_test = [] {
        Textures device;
        xrender::TexturePool pool(device);
        xrender::Graph g(pool);
        auto target = device.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 8, 8, target_usage, "target"});
        auto layer = g.transient({8, 8, mlibs::rhi::Format::rgba8_unorm, target_usage}, "layer");
        g.pass({.name = "test.layer", .writes = {*layer}}, [](mlibs::rhi::CommandList&) {});
        g.pass({.name = "test.composite", .reads = {*layer}, .writes = {*target}}, [](mlibs::rhi::CommandList&) {});
        g.pass({.name = "test.culled", .writes = {*g.transient({8, 8, mlibs::rhi::Format::rgba8_unorm, target_usage}, "x")}}, {});
        mlibs::trace::start();
        mlibs::rhi::CommandList list;
        expect(g.record(list, std::span(&*target, 1)).has_value());
        const auto t = mlibs::trace::stop();
        std::vector<std::string> spans;
        for (const auto& e : t.events)
            if (e.kind == mlibs::trace::Kind::begin && std::string_view(e.category) == "render") spans.emplace_back(e.name);
        expect(spans == std::vector<std::string>{"test.layer", "test.composite"}) << std::format("{}", spans);
    };
}};
