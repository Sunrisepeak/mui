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
    mlibs::rhi::Result<mlibs::rhi::Texture> make(const mlibs::rhi::TextureDesc& d) override { return textures_.insert({d.format, d.width, d.height, d.usage, {}}); }
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
