// Actual desktop device graph CTS; portable mock-device cases stay in render/tests.
import std;
import openmui.render;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.testing;
import mcpplibs.os;
using namespace omui;
using namespace omui::testing;
namespace {
constexpr auto target_usage = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;
std::unique_ptr<rhi::Device> open_device() {
    xrender::rhi::sdl::Options o;
    o.formats = rhi::formats({rhi::ShaderFormat::spirv, rhi::ShaderFormat::msl, rhi::ShaderFormat::dxil});
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
    auto d = xrender::rhi::sdl::create(o);
    if (!d) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message));
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    return std::move(*d);
}

} // namespace
int main(int argc, char** argv) {
    Suite s{"render.graph"};
    s.test("on a device: a layer made, copied to the target; what nothing reads never runs", [] {
        auto device = open_device();
        if (!device) return;
        xrender::TexturePool pool(*device);
        pool.begin_frame();
        constexpr std::uint32_t W = 24, H = 16;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, target_usage, "graph.target"});
        check(target.has_value());
        if (!target) return;
        xrender::Graph g(pool);
        const xrender::TextureNeed need{W, H, rhi::Format::rgba8_unorm, target_usage};
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
        xrender::Graph next(pool);
        check(next.transient(need, "red").has_value() && next.transient(need, "blue").has_value());
        check_eq(pool.made(), std::size_t{2});
        device->destroy(*target);
    });

    return s.run(argc, argv);
}
