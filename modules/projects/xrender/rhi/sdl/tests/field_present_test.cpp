// A cellular field on screen both ways (D-132), on whatever device this
// machine has: on the CPU, its cells coloured into an image (cellular.present)
// drawn by the CPU renderer; on the device, stepped there, painted there
// (cellular.rhi's Painter) and drawn by the GPU renderer as an external
// texture, never read back. One draw command each way, and the same pixels.
import std;
import mlibs.rhi;
import mlibs.geom;
import xrender.rhi.sdl;
import xrender.raster;
import xrender.vector;
import xengine.cellular;
import xengine.cellular.present;
import xengine.cellular.rhi;
import mcpplibs.os;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace ca = xengine::cellular;
namespace rhi = mlibs::rhi;
namespace geom = mlibs::geom;

UTest cases{"xrender.sdl.field", [] {
    "the field drawn by the GPU from the device's own cells is the CPU's picture, one command each"_test = [] {
        xrender::rhi::sdl::Options o;
        o.formats = xrender::gpu_shader_formats() & ca::gpu::shader_formats();
        if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
        if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
        auto created = xrender::rhi::sdl::create(o);
        if (!created) {
            if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << std::format("OMUI_REQUIRE_GPU=1 and no device: {}", created.error().message);
            return;
        }
        rhi::Device& device = **created;
        using C = ca::Condition;
        using W = ca::WriteKind;
        const std::vector<ca::Rule> rules{{{C::self({1, {1, 1}}), C::self({2, {3, 0xffffffffu}})}, {{1, W::set, 2}}},
                                          {{C::self({1, {1, 1}})}, {{2, W::add_saturating, 1}}},
                                          {{C::self({0, {1, 1}}), C::self({1, {0, 0}}), C::neighbors({1, {1, 1}}, ca::cardinal, {1, 4})}, {{1, W::set, 1}, {2, W::set, 0}}}};
        const ca::Shape shape{96, 64};
        const std::uint32_t area = shape.width * shape.height;
        std::vector<std::uint32_t> cells(area * 3, 0);
        for (std::uint32_t x = 2; x + 2 < shape.width; ++x) cells[32 * shape.width + x] = 1;
        for (std::uint32_t y = 2; y + 2 < shape.height; ++y) cells[y * shape.width + 30] = 1;
        cells[area + 32 * shape.width + 2] = 1;
        const auto program = ca::Program::prepare(3, rules);
        expect(program.has_value());
        if (!program) return;
        // The CPU path: stepped, presented, drawn by the CPU renderer.
        xrender::ImageStore store;
        auto view = ca::FieldPresenter::make(store, shape, 3,
                                             {{{{{1, {1, 1}}}, geom::Color::hex(0xff7a1a)}, {{{1, {2, 2}}}, geom::Color::hex(0x3a2a20)}, {{{0, {1, 1}}}, geom::Color::hex(0x8fa3b8)}},
                                              geom::Color::hex(0x101820)});
        expect(view.has_value());
        if (!view) return;
        auto cpu = cells, next = cells;
        // The device path: the same cells uploaded once, stepped and painted there.
        auto kernel = ca::gpu::Kernel::prepare(device, *program);
        auto painter = ca::gpu::Painter::prepare(device, view->pixels(), view->fallback_pixel(), 3);
        expect(kernel.has_value() && painter.has_value()) << (kernel ? (painter ? "" : painter.error().message) : kernel.error().message);
        if (!kernel || !painter) return;
        const rhi::BufferDesc desc{.size = area * 3 * 4, .usage = rhi::BufferUsage::compute_read | rhi::BufferUsage::compute_write};
        auto a = device.create(desc), b = device.create(desc);
        auto image = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, shape.width, shape.height, rhi::TextureUsage::compute_write | rhi::TextureUsage::sampled, "field"});
        auto target = device.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, shape.width * 4, shape.height * 4, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "screen"});
        expect(a && b && image && target);
        if (!a || !b || !image || !target) return;
        ca::gpu::Field in{&device, *a, shape, 3}, out{&device, *b, shape, 3};
        rhi::CommandList list;
        list.upload(*a, 0, std::span<const std::uint32_t>(cells));
        auto renderer = xrender::GpuRenderer::create(device);
        expect(renderer.has_value());
        if (!renderer) return;
        const xrender::Resources resources{&store, nullptr};
        const geom::Rect dest{0, 0, static_cast<float>(shape.width * 4), static_cast<float>(shape.height * 4)};
        for (int tick = 1; tick <= 40; ++tick) {
            expect(program->step(shape, cpu, next).has_value());
            cpu.swap(next);
            expect((*kernel)->record(list, in, out).has_value());
            std::swap(in, out);
            if (tick % 10) continue;
            // The device: one external texture command, the field painted just before it.
            expect((*painter)->record(list, in, *image).has_value());
            expect(device.submit(list).has_value());
            list.clear();
            xrender::DisplayList gpu_list;
            gpu_list.external_texture(1, dest, 1, xrender::Sampling::nearest);  // pixels that stay pixels
            expect(eq(gpu_list.commands().size(), std::size_t{1}));
            auto report = renderer->render(gpu_list, *target, resources, {.external = [&](std::uint64_t id) -> std::optional<rhi::Texture> {
                                               return id == 1 ? std::optional(*image) : std::nullopt;
                                           }});
            expect(report.has_value() && report->external_skipped == 0) << (report ? "" : report.error().message);
            auto bytes = device.read(*target, rhi::Region{0, 0, shape.width * 4, shape.height * 4});
            expect(bytes.has_value());
            // The CPU: one image command.
            (void)view->present(cpu);
            xrender::DisplayList cpu_list;
            view->draw(cpu_list, dest);
            expect(eq(cpu_list.commands().size(), std::size_t{1}));
            xrender::raster::Surface surface(static_cast<int>(shape.width * 4), static_cast<int>(shape.height * 4));
            xrender::CpuRenderer cpu_renderer;
            (void)cpu_renderer.render(cpu_list, surface, resources);
            if (!bytes) continue;
            std::size_t different = 0;
            for (std::size_t k = 0; k < surface.pixels().size(); ++k) {
                const auto& c = surface.pixels()[k];
                const auto* g = reinterpret_cast<const std::uint8_t*>(bytes->data()) + k * 4;
                different += std::max({std::abs(c.r - g[0]), std::abs(c.g - g[1]), std::abs(c.b - g[2]), std::abs(c.a - g[3])}) > 1;
            }
            expect(different == 0) << std::format("tick {}: {} pixels differ between the device's field and the CPU's", tick, different);
        }
        device.destroy(*a);
        device.destroy(*b);
        device.destroy(*image);
        device.destroy(*target);
    };
}};
