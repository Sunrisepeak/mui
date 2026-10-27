// The GPU backend against the CPU reference, scene by scene, on the
// machine's device (lavapipe in Linux CI; with OMUI_REQUIRE_GPU=1 a missing
// device fails). Each scene states its tolerance: what is computed the same
// way on both (axis-aligned rectangles, everything drawn from coverage the
// rasteriser made, gradient tables, nearest images, layers) agrees within
// blending's rounding. Rounded rectangles are where the two differ, and the
// GPU is the exact one: it covers a pixel by the area of the true circle (or,
// rotated, by signed distance), while the CPU flattens arcs to chords within
// 1/32 pixel, which loses at most ⅔ · 1/32 of a pixel where an arc runs
// nearly along a pixel's side: 255 · 2/3 / 32 ≈ 5.3 levels, plus a level of
// rounding on each side, so 8; only edge pixels may differ. Rotated, the
// GPU's distance is exact along straight edges and approximate across a
// corner or an arc, which the transforms scene allows two levels more.
import std;
import mcpplibs.os;
import openmui.geom;
import openmui.raster;
import openmui.render;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;
namespace rhi = omui::rhi;

namespace {

constexpr std::uint32_t W = 160, H = 120;

bool required() { return mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1"; }

std::unique_ptr<rhi::Device> open_device() {
    rhi::sdl::Options o;
    o.formats = render::gpu_shader_formats();
    if (auto lib = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) o.vulkan_library = *lib;
    if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) o.video_driver = *video;
    auto d = rhi::sdl::create(o);
    if (!d) {
        if (required()) check(false, std::format("OMUI_REQUIRE_GPU=1 and no device: {}", d.error().message));
        else std::println("no GPU device here ({}); set OMUI_REQUIRE_GPU=1 to make that a failure", d.error().message);
        return nullptr;
    }
    return std::move(*d);
}

// Glyphs without fonts: glyph n is a box (n + 2) / 10 em wide and 0.7 em
// tall, its masks made by the rasteriser at the subpixel offset asked for.
class Boxes final : public render::GlyphSource {
public:
    render::GlyphMask mask(render::FontRef, std::uint32_t glyph, float size, geom::Point fraction) override {
        const auto key = std::format("{}/{}/{}/{}", glyph, size, fraction.x, fraction.y);
        auto it = masks_.find(key);
        if (it == masks_.end()) {
            raster::PathBuilder pb;
            const float w = (static_cast<float>(glyph) + 2) / 10 * size, h = 0.7f * size;
            pb.rect({fraction.x, fraction.y - h, w, h});
            auto c = std::make_unique<raster::Coverage>();
            raster::fill_coverage(pb.data(), {}, raster::FillRule::nonzero, {-1000, -1000, 4000, 4000}, *c);
            it = masks_.emplace(key, std::move(c)).first;
        }
        return {it->second.get(), nullptr, {}};
    }
    raster::PathData outline(render::FontRef, std::uint32_t glyph) override {
        auto it = outlines_.find(glyph);
        if (it == outlines_.end()) {
            auto pb = std::make_unique<raster::PathBuilder>();
            pb->rect({0, -0.7f, (static_cast<float>(glyph) + 2) / 10, 0.7f});
            it = outlines_.emplace(glyph, std::move(pb)).first;
        }
        return it->second->data();
    }

private:
    std::map<std::string, std::unique_ptr<raster::Coverage>> masks_;
    std::map<std::uint32_t, std::unique_ptr<raster::PathBuilder>> outlines_;
};

struct Tolerance {
    int         max_diff;        // the largest channel difference anywhere
    std::size_t max_off_pixels;  // pixels differing by more than 1
};

struct Rig {
    rhi::Device&         d;
    render::GpuRenderer  gpu;
    render::CpuRenderer  cpu;
    rhi::Texture         target;
    render::ImageStore   images;
    Boxes                glyphs;

    // Both renders of `dl`; how they differ.
    std::string compare(const render::DisplayList& dl, std::string_view name, Tolerance t) {
        const geom::Color bg = geom::Color::hex(0xf8fafc);
        raster::Surface ref(W, H);
        cpu.render(dl, ref, {&images, &glyphs}, {.scale = 1, .background = bg});
        auto report = gpu.render(dl, target, {&images, &glyphs}, {.scale = 1, .background = bg});
        if (!report) return std::format("{}: the GPU render failed: {}", name, report.error().message);
        auto bytes = d.read(target, {0, 0, W, H});
        if (!bytes) return std::format("{}: read back: {}", name, bytes.error().message);
        int worst = 0;
        std::size_t off = 0;
        std::uint32_t wx = 0, wy = 0;
        for (std::uint32_t y = 0; y < H; ++y)
            for (std::uint32_t x = 0; x < W; ++x) {
                const raster::Rgba8 c = ref.at(static_cast<int>(x), static_cast<int>(y));
                const auto* g = reinterpret_cast<const std::uint8_t*>(bytes->data()) + (y * W + x) * 4;
                const int diff = std::max({std::abs(c.r - g[0]), std::abs(c.g - g[1]), std::abs(c.b - g[2]), std::abs(c.a - g[3])});
                if (diff > 1) ++off;
                if (diff > worst) worst = diff, wx = x, wy = y;
            }
        std::println("    {:<12} max diff {:>3}, {:>4} pixels off by more than 1 (allowed {} and {})", name, worst, off, t.max_diff, t.max_off_pixels);
        if (worst <= t.max_diff && off <= t.max_off_pixels) return {};
        // Both images, to look at.
        const auto dir = scratch_dir("render-gpu");
        omui::imageio::View cv{reinterpret_cast<const std::uint8_t*>(ref.pixels().data()), W, H, W * 4};
        omui::imageio::View gv{reinterpret_cast<const std::uint8_t*>(bytes->data()), W, H, W * 4};
        static_cast<void>(omui::imageio::save_png(dir / std::format("{}.cpu.png", name), cv));
        static_cast<void>(omui::imageio::save_png(dir / std::format("{}.gpu.png", name), gv));
        const raster::Rgba8 c = ref.at(static_cast<int>(wx), static_cast<int>(wy));
        const auto* g = reinterpret_cast<const std::uint8_t*>(bytes->data()) + (wy * W + wx) * 4;
        return std::format("{}: max diff {} at ({}, {}): cpu ({}, {}, {}, {}) gpu ({}, {}, {}, {}); {} pixels off (images in {})", name, worst,
                           wx, wy, c.r, c.g, c.b, c.a, g[0], g[1], g[2], g[3], off, dir.generic_string());
    }
};

render::Paint gradient(render::GradientKind kind, geom::Point a, geom::Point b, float radius, render::Spread spread,
                       std::span<const render::GradientStop> stops) {
    render::Paint p;
    p.kind = render::Paint::Kind::gradient;
    p.gradient.kind = kind;
    p.gradient.start = a;
    p.gradient.end = b;
    p.gradient.radius = radius;
    p.gradient.spread = spread;
    p.gradient.stops = stops;
    return p;
}

const std::array<render::GradientStop, 3> rainbow{{{0, geom::Color::hex(0xef4444)}, {0.5f, geom::Color::hex(0x22c55e)}, {1, geom::Color{0.2f, 0.3f, 0.9f, 0.6f}}}};

using Scene = std::function<void(render::DisplayList&, Rig&)>;
struct Case {
    std::string_view name;
    Scene            build;
    Tolerance        tolerance;
};

const std::vector<Case>& cases() {
    static const std::vector<Case> all{
        {"rects",
         [](render::DisplayList& dl, Rig&) {
             dl.fill_rect({10, 10, 60, 40}, geom::Color::hex(0x6366f1));
             dl.fill_rect({30.25f, 20.5f, 70.5f, 50.75f}, geom::Color{0.9f, 0.2f, 0.3f, 0.5f});
             dl.fill_rect({0.4f, 90.6f, 159.2f, 0.8f}, geom::Color::hex(0x0f172a));
             dl.fill_rect({120.5f, 5, 30, 100}, geom::Color{0, 0.6f, 0.4f, 0.75f});
         },
         {1, 0}},
        {"gradients",
         [](render::DisplayList& dl, Rig&) {
             dl.fill_rect({0, 0, 80, 60}, gradient(render::GradientKind::linear, {0, 0}, {80, 30}, 0, render::Spread::pad, rainbow));
             dl.fill_rect({80, 0, 80, 60}, gradient(render::GradientKind::radial, {120, 30}, {}, 18, render::Spread::repeat, rainbow));
             dl.fill_rect({0, 60, 80, 60}, gradient(render::GradientKind::linear, {20, 80}, {40, 100}, 0, render::Spread::reflect, rainbow));
             dl.fill_rect({80, 60, 80, 60}, gradient(render::GradientKind::conic, {120, 90}, {}, 0.5f, render::Spread::pad, rainbow));
         },
         {1, 4}},
        {"rounded",
         [](render::DisplayList& dl, Rig&) {
             dl.fill_round_rect({10, 10, 70, 50}, geom::CornerRadii::all(12), render::Paint::solid(geom::Color::hex(0x0ea5e9)));
             dl.fill_round_rect({60.5f, 40.25f, 80, 60}, {4, 20, 0, 30}, render::Paint::solid(geom::Color{0.95f, 0.6f, 0.1f, 0.7f}));
             dl.fill_round_rect({100, 8, 50, 30}, geom::CornerRadii::all(40), render::Paint::solid(geom::Color::hex(0x16a34a)));
         },
         {8, 240}},
        {"strokes",
         [](render::DisplayList& dl, Rig&) {
             dl.stroke_round_rect({10.5f, 10.5f, 60, 40}, geom::CornerRadii::all(8), 3, render::Paint::solid(geom::Color::hex(0x7c3aed)));
             dl.stroke_round_rect({90, 10, 50, 40}, {}, 2, render::Paint::solid(geom::Color::hex(0xdc2626)));
             render::Path star;
             star.move_to({40, 62}).line_to({52, 110}).line_to({12, 80}).line_to({68, 80}).line_to({28, 110}).close();
             dl.fill_path(star, render::FillRule::even_odd, gradient(render::GradientKind::linear, {12, 62}, {68, 110}, 0, render::Spread::pad, rainbow));
             render::Path wave;
             wave.move_to({80, 90}).cubic_to({100, 60}, {120, 120}, {150, 80});
             const std::array<float, 2> dashes{8, 4};
             dl.stroke_path(wave, {.width = 4, .cap = render::LineCap::round, .dashes = dashes}, render::Paint::solid(geom::Color::hex(0x0891b2)));
         },
         {1, 0}},
        {"shadows",
         [](render::DisplayList& dl, Rig&) {
             dl.box_shadow({20, 20, 50, 40}, geom::CornerRadii::all(8), {0, 4}, 12, 2, {0, 0, 0, 0.35f});
             dl.fill_rect({20, 20, 50, 40}, geom::Color::white());
             dl.box_shadow({95, 25, 50, 50}, geom::CornerRadii::all(10), {3, 3}, 8, 0, {0.1f, 0.1f, 0.4f, 0.5f}, true);
         },
         {1, 0}},
        {"clips",
         [](render::DisplayList& dl, Rig&) {
             dl.push_clip({10.5f, 10.5f, 140, 100});
             dl.push_clip({20, 20, 120, 80}, geom::CornerRadii::all(24));
             dl.fill_rect({0, 0, 160, 120}, gradient(render::GradientKind::linear, {0, 0}, {160, 0}, 0, render::Spread::pad, rainbow));
             render::Path circle;
             circle.add_circle({80, 60}, 30);
             dl.push_clip(circle, render::FillRule::nonzero);
             dl.fill_rect({0, 0, 160, 120}, geom::Color{0, 0, 0, 0.5f});
             dl.pop_clip();
             dl.pop_clip();
             dl.pop_clip();
         },
         {1, 0}},
        {"transforms",
         [](render::DisplayList& dl, Rig&) {
             dl.push_transform(geom::Affine::translate(50, 60) * render::rotate(0.35f));
             dl.fill_rect({-25, -15, 50, 30}, geom::Color::hex(0x2563eb));
             render::Path tri;
             tri.move_to({-10, -30}).line_to({20, -40}).line_to({0, -20}).close();
             dl.fill_path(tri, render::FillRule::nonzero, render::Paint::solid(geom::Color::hex(0xf97316)));
             dl.pop_transform();
             dl.push_transform(geom::Affine::translate(115, 60) * render::rotate(-0.6f) * geom::Affine::scale(1.5f, 1.5f));
             dl.fill_round_rect({-15, -10, 30, 20}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0x059669)));
             dl.pop_transform();
         },
         {10, 100}},
        {"layers",
         [](render::DisplayList& dl, Rig&) {
             dl.push_layer(0.5f);
             dl.fill_rect({10, 10, 80, 60}, geom::Color::hex(0xdc2626));
             dl.fill_rect({50, 40, 80, 60}, geom::Color::hex(0x2563eb));
             dl.push_layer(0.6f);
             dl.fill_rect({30, 30, 100, 30}, geom::Color::hex(0x16a34a));
             dl.pop_layer();
             dl.pop_layer();
         },
         {2, 0}},
        {"images",
         [](render::DisplayList& dl, Rig& r) {
             std::vector<raster::Rgba8> px(16 * 16);
             for (int y = 0; y < 16; ++y)
                 for (int x = 0; x < 16; ++x) {
                     const auto v = static_cast<std::uint8_t>((x ^ y) * 16);
                     px[static_cast<std::size_t>(y * 16 + x)] = {v, static_cast<std::uint8_t>(255 - v), 128, 255};
                 }
             const render::ImageRef img = r.images.add(16, 16, std::move(px));
             dl.draw_image(img, {0, 0, 16, 16}, {8, 8, 64, 64}, render::Sampling::nearest, 1);
             dl.draw_image(img, {4, 4, 8, 8}, {88, 8, 64, 64}, render::Sampling::nearest, 0.6f);
             render::Paint pattern;
             pattern.kind = render::Paint::Kind::image;
             pattern.image = img;
             pattern.color = geom::Color{1, 1, 1, 0.8f};
             pattern.sampling = render::Sampling::nearest;
             pattern.image_transform = geom::Affine::scale(2, 2);
             dl.fill_rect({10, 80, 140, 32}, pattern);
         },
         {1, 0}},
        {"glyphs",
         [](render::DisplayList& dl, Rig&) {
             std::array<render::Glyph, 6> run{};
             for (std::uint32_t i = 0; i < run.size(); ++i) run[i] = {i, {static_cast<float>(i) * 14.3f, 0}, i, i, 0, 0};
             dl.draw_glyphs({1}, 16, {8.4f, 30.6f}, run, render::Paint::solid(geom::Color::hex(0x0f172a)));
             std::array<render::GlyphStyle, 6> styles{};
             styles[1].opacity = 0.5f;
             styles[2].tint = geom::Color{1, 0, 0, 1};
             styles[3].transform = geom::Affine::translate(0, -6);
             styles[4].transform = render::rotate(0.4f);
             dl.draw_glyphs({1}, 20, {8, 80}, run, render::Paint::solid(geom::Color::hex(0x1d4ed8)), styles);
             dl.draw_glyphs({1}, 14, {8, 110}, run,
                            gradient(render::GradientKind::linear, {8, 0}, {120, 0}, 0, render::Spread::pad, rainbow));
         },
         {1, 0}},
    };
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.gpu"};

    s.test("each scene as the CPU reference draws it, within the scene's tolerance", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        check(gpu.has_value(), gpu ? "" : gpu.error().message);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H,
                                                      rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "test.target"});
        check(target.has_value());
        if (!target) return;
        Rig rig{*device, std::move(*gpu), {}, *target, {}, {}};
        for (const Case& c : cases()) {
            render::DisplayList dl;
            c.build(dl, rig);
            const std::string problem = rig.compare(dl, c.name, c.tolerance);
            check(problem.empty(), problem);
        }
    });

    s.test("what the GPU does not draw yet is counted, as the CPU counts what it skips", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 8, 8, rhi::TextureUsage::color_target, "test.small"});
        if (!target) return;
        render::DisplayList dl;
        dl.push_layer(1, render::BlendMode::multiply);
        dl.fill_rect({0, 0, 8, 8}, geom::Color::white());
        dl.pop_layer();
        dl.external_texture(7, {0, 0, 4, 4});
        auto report = gpu->render(dl, *target, {});
        check(report.has_value());
        if (report) check(report->blends_skipped == 1 && report->external_skipped == 1 && report->commands == dl.size());
    });

    return s.run(argc, argv);
}
