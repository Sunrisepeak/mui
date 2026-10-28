// The rendering architecture (D-81): transient textures pooled from frame
// to frame; a frame's passes checked (a transient read before it is written
// is named), planned (work that reaches no output is left out, the order
// kept) and recorded, each pass in a trace span — against a device that only
// remembers what it made, and on a real one, where the bytes say which
// passes ran.
import std;
import openmui.render;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.memory;
import openmui.trace;
import openmui.testing;
import mcpplibs.os;

using namespace omui::testing;
using namespace omui;

namespace {

// Remembers the textures it made; runs nothing.
class Textures final : public rhi::Device {
public:
    rhi::Caps caps_{.backend = rhi::Backend::vulkan, .device = "textures", .formats = rhi::formats({rhi::ShaderFormat::spirv})};
    std::size_t destroyed = 0;

    const rhi::Caps& caps() const noexcept override { return caps_; }
    void destroy(rhi::Buffer) noexcept override {}
    void destroy(rhi::Texture t) noexcept override {
        if (textures_.get(t)) ++destroyed;
        textures_.erase(t);
    }
    void destroy(rhi::Sampler) noexcept override {}
    void destroy(rhi::GraphicsPipeline) noexcept override {}
    void destroy(rhi::ComputePipeline) noexcept override {}
    std::optional<rhi::BufferDesc> describe(rhi::Buffer) const noexcept override { return std::nullopt; }
    std::optional<rhi::TextureDesc> describe(rhi::Texture t) const noexcept override {
        auto* d = textures_.get(t);
        return d ? std::optional(*d) : std::nullopt;
    }
    bool exists(rhi::Sampler) const noexcept override { return false; }
    std::optional<rhi::GraphicsPipelineInfo> describe(rhi::GraphicsPipeline) const noexcept override { return std::nullopt; }
    std::optional<rhi::ComputePipelineInfo> describe(rhi::ComputePipeline) const noexcept override { return std::nullopt; }
    rhi::Result<void> wait(rhi::Submission) override { return {}; }
    rhi::Result<void> wait_idle() override { return {}; }
    [[nodiscard]] std::size_t live() const noexcept { return textures_.size(); }

protected:
    rhi::Result<rhi::Buffer> make(const rhi::BufferDesc&) override { return rhi::fail(rhi::Errc::unsupported, "textures only"); }
    rhi::Result<rhi::Texture> make(const rhi::TextureDesc& d) override { return textures_.insert({d.format, d.width, d.height, d.usage, {}}); }
    rhi::Result<rhi::Sampler> make(const rhi::SamplerDesc&) override { return rhi::fail(rhi::Errc::unsupported, "textures only"); }
    rhi::Result<rhi::GraphicsPipeline> make(const rhi::GraphicsPipelineDesc&) override { return rhi::fail(rhi::Errc::unsupported, "textures only"); }
    rhi::Result<rhi::ComputePipeline> make(const rhi::ComputePipelineDesc&) override { return rhi::fail(rhi::Errc::unsupported, "textures only"); }
    rhi::Result<rhi::Submission> run(const rhi::CommandList&) override { return rhi::Submission{1}; }
    rhi::Result<void> fetch(rhi::Texture, rhi::Region, std::span<std::byte>) override { return {}; }
    rhi::Result<void> fetch(rhi::Buffer, std::uint32_t, std::span<std::byte>) override { return {}; }

private:
    SlotMap<rhi::TextureDesc, rhi::TextureTag> textures_;
};

constexpr auto target_usage = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;

std::unique_ptr<rhi::Device> open_device() {
    rhi::sdl::Options o;
    o.formats = rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl, rhi::ShaderFormat::dxil});
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
    auto d = rhi::sdl::create(o);
    if (!d) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message));
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    return std::move(*d);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.graph"};

    s.test("the pool hands a texture out again in later frames, and lets go of one nobody asks for", [] {
        Textures device;
        render::TexturePool pool(device, 2);
        const render::TextureNeed layer{64, 32, rhi::Format::rgba8_unorm, target_usage};
        pool.begin_frame();
        auto a = pool.acquire(layer, "a");
        auto b = pool.acquire(layer, "b");
        check(a && b && *a != *b, "two of the same need in one frame are two textures");
        pool.begin_frame();
        auto again = pool.acquire(layer, "a");
        check(again && (*again == *a || *again == *b), "the next frame gets one of them back");
        check_eq(pool.made(), std::size_t{2});
        // The frame encoded again from the start: what it was handed is free.
        pool.restart_frame();
        auto restarted = pool.acquire(layer, "a");
        check(restarted && pool.made() == 2, "a frame started over makes nothing new");
        auto other = pool.acquire({64, 32, rhi::Format::r8_unorm, rhi::TextureUsage::sampled}, "mask");
        check(other && pool.made() == 3, "another format is another texture");
        // Frames without asking: after `keep` of them they go.
        for (int i = 0; i < 4; ++i) pool.begin_frame();
        check_eq(pool.held(), std::size_t{0});
        check_eq(device.live(), std::size_t{0});
        check(pool.released() == 3, std::format("{} released", pool.released()));
    });

    s.test("a pass that reads a transient texture before any pass writes it is named", [] {
        Textures device;
        render::TexturePool pool(device);
        render::Graph g(pool);
        auto target = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 16, 16, target_usage, "target"});
        auto layer = g.transient({16, 16, rhi::Format::rgba8_unorm, target_usage}, "the layer");
        check(target && layer && g.transient(*layer) && !g.transient(*target));
        g.pass({.name = "ui.composite", .label = "node 7", .reads = {*layer}, .writes = {*target}}, {});
        g.pass({.name = "ui.layer", .writes = {*layer}}, {});
        auto plan = g.plan(std::span(&*target, 1));
        check(!plan, "not planned");
        if (!plan) {
            check_eq(plan.error().size(), std::size_t{1});
            check_eq(plan.error()[0].pass, std::size_t{0});
            check_eq(plan.error()[0].text, std::string("ui.composite (node 7) reads `the layer` before any pass writes it"));
        }
        // A texture from outside the graph is the frame's to read.
        render::Graph h(pool);
        h.pass({.name = "ui.copy", .reads = {*target}, .writes = {*target}}, {});
        check(h.plan(std::span(&*target, 1)).has_value(), "the target holds what it held");
    });

    s.test("work that reaches no output is left out; the order is the description's", [] {
        Textures device;
        render::TexturePool pool(device);
        render::Graph g(pool);
        auto target = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 32, 32, target_usage, "target"});
        auto atlas = device.create(rhi::TextureDesc{rhi::Format::r8_unorm, 32, 32, target_usage, "atlas"});
        const render::TextureNeed need{32, 32, rhi::Format::rgba8_unorm, target_usage};
        auto used = g.transient(need, "used layer");
        auto unused = g.transient(need, "a layer nobody composites");
        auto only = g.transient(need, "read only by the unused");
        std::vector<std::string> ran;
        auto log = [&](std::string name) { return [&ran, name](rhi::CommandList&) { ran.push_back(name); }; };
        g.pass({.name = "clear", .writes = {*target}}, log("clear"));
        g.pass({.name = "only", .writes = {*only}}, log("only"));
        g.pass({.name = "unused", .reads = {*only}, .writes = {*unused}}, log("unused"));
        g.pass({.name = "used", .writes = {*used}}, log("used"));
        g.pass({.name = "atlas", .writes = {*atlas}}, log("atlas"));  // outside the graph: kept
        g.pass({.name = "composite", .reads = {*used}, .writes = {*target}}, log("composite"));
        auto plan = g.plan(std::span(&*target, 1));
        check(plan.has_value());
        if (plan) {
            check(plan->run == std::vector<std::size_t>{0, 3, 4, 5}, std::format("ran {}", plan->run));
            check(plan->culled == std::vector<std::size_t>{1, 2}, std::format("culled {}", plan->culled));
        }
        rhi::CommandList list;
        auto report = g.record(list, std::span(&*target, 1));
        check(report && report->recorded == 4 && report->culled == 2 && report->transients == 3);
        check(ran == std::vector<std::string>{"clear", "used", "atlas", "composite"}, std::format("{}", ran));
        g.clear();
        check(g.size() == 0 && !g.transient(*used), "cleared for the next frame");
    });

    s.test("each pass that runs is a trace span of its name", [] {
        Textures device;
        render::TexturePool pool(device);
        render::Graph g(pool);
        auto target = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 8, 8, target_usage, "target"});
        auto layer = g.transient({8, 8, rhi::Format::rgba8_unorm, target_usage}, "layer");
        g.pass({.name = "test.layer", .writes = {*layer}}, [](rhi::CommandList&) {});
        g.pass({.name = "test.composite", .reads = {*layer}, .writes = {*target}}, [](rhi::CommandList&) {});
        g.pass({.name = "test.culled", .writes = {*g.transient({8, 8, rhi::Format::rgba8_unorm, target_usage}, "x")}}, {});
        trace::start();
        rhi::CommandList list;
        check(g.record(list, std::span(&*target, 1)).has_value());
        const auto t = trace::stop();
        std::vector<std::string> spans;
        for (const auto& e : t.events)
            if (e.kind == trace::Kind::begin && std::string_view(e.category) == "render") spans.emplace_back(e.name);
        check(spans == std::vector<std::string>{"test.layer", "test.composite"}, std::format("{}", spans));
    });

    s.test("on a device: a layer made, copied to the target; what nothing reads never runs", [] {
        auto device = open_device();
        if (!device) return;
        render::TexturePool pool(*device);
        pool.begin_frame();
        constexpr std::uint32_t W = 24, H = 16;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, target_usage, "graph.target"});
        check(target.has_value());
        if (!target) return;
        render::Graph g(pool);
        const render::TextureNeed need{W, H, rhi::Format::rgba8_unorm, target_usage};
        auto red = g.transient(need, "red");
        auto blue = g.transient(need, "blue");
        check(red && blue);
        g.pass({.name = "test.clear", .writes = {*target}}, [&](rhi::CommandList& l) {
            l.begin_render({*target, rhi::LoadOp::clear, {0, 0, 0, 1}});
            l.end_render();
        });
        g.pass({.name = "test.red", .writes = {*red}}, [&](rhi::CommandList& l) {
            l.begin_render({*red, rhi::LoadOp::clear, {1, 0, 0, 1}});
            l.end_render();
        });
        g.pass({.name = "test.blue", .writes = {*blue}}, [&](rhi::CommandList& l) {
            l.begin_render({*blue, rhi::LoadOp::clear, {0, 0, 1, 1}});
            l.end_render();
        });
        // The left half from the red layer; the blue one is read by nothing.
        g.pass({.name = "test.copy", .reads = {*red}, .writes = {*target}},
               [&](rhi::CommandList& l) { l.copy(*red, rhi::Region{0, 0, W / 2, H}, *target, 0, 0); });
        rhi::CommandList list;
        auto report = g.record(list, std::span(&*target, 1));
        check(report && report->recorded == 3 && report->culled == 1);
        auto submitted = device->submit(list);
        check(submitted.has_value(), submitted ? "" : submitted.error().message);
        auto px = device->read(*target, rhi::Region{0, 0, W, H});
        check(px.has_value());
        if (px) {
            auto at = [&](std::uint32_t x, std::uint32_t y) {
                const std::size_t i = (y * W + x) * 4;
                return std::array{std::to_integer<int>((*px)[i]), std::to_integer<int>((*px)[i + 1]), std::to_integer<int>((*px)[i + 2]),
                                  std::to_integer<int>((*px)[i + 3])};
            };
            check(at(0, 0) == std::array{255, 0, 0, 255} && at(W / 2 - 1, H - 1) == std::array{255, 0, 0, 255},
                  std::format("left {}", at(0, 0)));
            check(at(W / 2, 0) == std::array{0, 0, 0, 255} && at(W - 1, H - 1) == std::array{0, 0, 0, 255},
                  std::format("right {}: the blue layer never ran", at(W - 1, 0)));
        }
        // The next frame reuses both layers: nothing made.
        pool.begin_frame();
        render::Graph next(pool);
        check(next.transient(need, "red").has_value() && next.transient(need, "blue").has_value());
        check_eq(pool.made(), std::size_t{2});
        device->destroy(*target);
    });

    return s.run(argc, argv);
}
