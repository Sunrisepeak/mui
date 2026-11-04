// Required-device runs exercise the real GPU pipelines and read back pixels.
import std;
import mcpplibs.os;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.testing;
using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
namespace rhi = omui::rhi;
namespace {
constexpr std::uint32_t W = 128, H = 96;
std::unique_ptr<rhi::Device> open_device() {
    rhi::sdl::Options options; options.formats = render::gpu_shader_formats();
    if (auto v = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *v;
    if (auto v = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *v;
    auto device = rhi::sdl::create(options);
    if (!device) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") check(false, device.error().message);
        else std::println("    no GPU: {}; OMUI_REQUIRE_GPU=1 makes this fail", device.error().message);
        return {};
    }
    return std::move(*device);
}
render::Path contour() {
    render::Path path;
    path.move_to({63.25f, 8.5f}).cubic_to({112, 5}, {120, 80}, {80, 84})
        .line_to({11.5f, 70.25f}).quad_to({8, 30}, {63.25f, 8.5f}).close(); return path;
}
void compare(rhi::Device& device, render::GpuRenderer& gpu, rhi::Texture target,
             const render::DisplayList& list, render::ImageStore* images = nullptr) {
    raster::Surface reference(W, H); render::CpuRenderer cpu;
    const auto cr = cpu.render(list, reference, {images, nullptr});
    check_eq(cr.invalid_paints, std::size_t{0}); check_eq(cr.missing_images, std::size_t{0});
    const auto report = gpu.render(list, target, {images, nullptr});
    check(report.has_value(), report ? "" : report.error().message); if (!report) return;
    check_eq(report->invalid_paints, std::size_t{0}); check_eq(report->missing_images, std::size_t{0});
    const auto bytes = device.read(target, {0, 0, W, H});
    check(bytes.has_value()); if (!bytes) return;
    unsigned worst = 0, off = 0;
    for (std::uint32_t y = 0; y < H; ++y) for (std::uint32_t x = 0; x < W; ++x) {
        const auto a = reference.at(static_cast<int>(x), static_cast<int>(y));
        const auto* b = reinterpret_cast<const std::uint8_t*>(bytes->data()) + (y * W + x) * 4;
        const auto difference = static_cast<unsigned>(std::max({std::abs(a.r - b[0]), std::abs(a.g - b[1]), std::abs(a.b - b[2]), std::abs(a.a - b[3])}));
        worst = std::max(worst, difference); off += difference > 2 ? 1u : 0u;
    }
    // Leaf shading and coverage use the existing CPU/GPU contract. Mixture
    // fields accumulate in rgba16_float, then quantize to RGBA8 once; allow
    // two levels for that precision and the final device blend rounding.
    std::println("    material GPU readback: max {} /255, pixels beyond 2: {}", worst, off);
    check(worst <= 2); check_eq(off, 0u);
}
}
int main(int argc, char** argv) {
    Suite suite{"render.paint-mix-gpu"};
    suite.test("gradient and differently lit image fields mix before a shared path and clip mask", [] {
        auto device = open_device(); if (!device) return;
        auto gpu = render::GpuRenderer::create(*device); check(gpu.has_value()); if (!gpu) return;
        const auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled});
        check(target.has_value()); if (!target) return;
        const std::array<render::GradientStop, 2> stops{{{0, geom::Color{0.9f, 0.1f, 0.2f, 0.4f}}, {1, geom::Color{0.1f, 0.9f, 0.2f, 0.9f}}}};
        render::Paint gradient; gradient.kind = render::Paint::Kind::gradient; gradient.gradient.start = {0, 0};
        gradient.gradient.end = {W, H}; gradient.gradient.stops = stops;
        render::ImageStore images;
        const auto image = images.add(2, 2, std::vector<raster::Rgba8>{{180, 120, 60, 255}, {40, 80, 160, 200}, {10, 30, 70, 100}, {20, 100, 40, 180}});
        const auto normal = images.add(2, 2, std::vector<raster::Rgba8>(4, raster::Rgba8{128, 128, 255, 255}));
        render::Material material; material.normal = normal; material.key = {0.7f, 0.4f, 1, 1};
        render::Paint picture; picture.kind = render::Paint::Kind::image; picture.image = image; picture.material = &material;
        picture.color = geom::Color::white(); picture.image_transform = geom::Affine::translate(7.25f, 3.5f) * geom::Affine::scale(55, 35);
        const auto plan = render::PreparedPaintMix::prepare(gradient, picture); check(plan.has_value()); if (!plan) return;
        for (double t : {0.0, 0.25, 0.5, 0.75, 1.0, 0.37}) {
            render::DisplayList list;
            list.push_clip({14, 9, 95, 72}, geom::CornerRadii::all(10));
            list.push_transform(geom::Affine::translate(1.25f, 2.5f));
            const auto paint = plan->record(t, list); check(paint.has_value()); if (!paint) continue;
            list.fill_path(contour(), render::FillRule::nonzero, *paint);
            list.pop_transform(); list.pop_clip();
            compare(*device, *gpu, *target, list, &images);
        }
        device->destroy(*target);
    });
    suite.test("sixteen distinct translucent leaves and one opaque mixture retain once-only coverage", [] {
        auto device = open_device(); if (!device) return;
        auto gpu = render::GpuRenderer::create(*device); check(gpu.has_value()); if (!gpu) return;
        const auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled});
        check(target.has_value()); if (!target) return;
        std::array<render::WeightedPaint, 16> terms;
        for (std::size_t i = 0; i < terms.size(); ++i) {
            const auto f = static_cast<float>(i) / 15;
            terms[i] = {render::Paint::solid({f, 1 - f, 0.7f, 0.2f + f * 0.7f}), 1.0f / 16};
        }
        render::Paint paint; paint.kind = render::Paint::Kind::mix; paint.mix = terms;
        render::DisplayList list; list.fill_path(contour(), render::FillRule::nonzero, paint);
        compare(*device, *gpu, *target, list);
        for (auto& term : terms) term.paint.color.a = 1;
        list.clear(); list.stroke_path(contour(), {.width = 5.5f, .join = render::LineJoin::round}, paint);
        compare(*device, *gpu, *target, list);
        device->destroy(*target);
    });
    return suite.run(argc, argv);
}
