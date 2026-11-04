// The GPU backend against the CPU reference, scene by scene, on the
// machine's device (lavapipe on Linux and Windows CI, Metal on macOS; with
// OMUI_REQUIRE_GPU=1 a missing device fails). The GPU works in the CPU's
// bytes — coverage as raster makes it (rectangles in 1/256 pixel, the
// rasteriser's masks), premultiplied 8-bit colours, gradient tables at the
// same index, images sampled with the same integer weights, every product a
// rounded mul255 — and leaves source-over to the blending unit, which on an
// exact unit (lavapipe) gives the CPU's bytes: those scenes allow a level, for
// units that round a last bit differently.
//
// Rounded rectangles are where the two differ, and the GPU is the exact one:
// it covers a pixel by the area of the true circle (or, rotated, by signed
// distance), while the CPU flattens arcs to chords within 1/32 pixel, which
// loses at most ⅔ · 1/32 of a pixel where an arc runs nearly along a pixel's
// side: 255 · 2/3 / 32 ≈ 5.3 levels, plus a level of rounding on each side,
// so 8; only edge pixels may differ. Rotated, the GPU's distance is exact
// along straight edges and approximate across a corner or an arc, which the
// transforms scene allows two levels more.
import std;
import mcpplibs.os;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.rhi;
import openmui.rhi.sdl;
import openmui.image;
import openmui.testing;
import openmui.trace;

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
    render::RenderReport last{};  // the GPU's, of the last compare

    // Both renders of `dl`; how they differ.
    std::string compare(const render::DisplayList& dl, std::string_view name, Tolerance t) {
        const geom::Color bg = geom::Color::hex(0xf8fafc);
        raster::Surface ref(W, H);
        cpu.render(dl, ref, {&images, &glyphs}, {.scale = 1, .background = bg});
        auto report = gpu.render(dl, target, {&images, &glyphs}, {.scale = 1, .background = bg});
        if (!report) return std::format("{}: the GPU render failed: {}", name, report.error().message);
        last = *report;
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

// Liquid glass (D-64) over a busy backdrop — stripes, a gradient, a ring —
// with a mark on the glass as its content; one shape, or a group's.
void glass_scene(render::DisplayList& dl, std::vector<std::pair<std::string_view, float>> values, std::vector<render::EffectShape> shapes = {}) {
    dl.fill_rect({0, 0, 160, 120}, geom::Color::hex(0xf8fafc));
    for (int i = 0; i < 8; ++i)
        dl.fill_rect({static_cast<float>(i) * 20.0f + 2.5f, 0, 9, 120}, geom::Color{0.15f + 0.1f * static_cast<float>(i), 0.3f, 0.8f - 0.08f * static_cast<float>(i), 1});
    dl.fill_rect({0, 70, 160, 50}, gradient(render::GradientKind::linear, {0, 70}, {160, 120}, 0, render::Spread::pad, rainbow));
    render::Path ring;
    ring.add_circle({80, 60}, 30).add_circle({80, 60}, 22);
    dl.fill_path(ring, render::FillRule::even_odd, render::Paint::solid(geom::Color::hex(0x111827)));
    const geom::Rect layer{30.5f, 25, 100, 70};
    const auto ref = render::effects().find("liquid_glass");
    check(ref.has_value(), "liquid_glass is a built-in");
    if (!ref) return;
    if (shapes.empty()) shapes.push_back({layer, geom::CornerRadii::all(24)});
    const auto params = render::effects().params(*ref, values);
    dl.push_layer(1, render::BlendMode::normal, *ref, params, layer, shapes);
    render::Path mark;
    mark.move_to({72, 50}).line_to({92, 60}).line_to({72, 70}).close();
    dl.fill_path(mark, render::FillRule::nonzero, render::Paint::solid(geom::Color{0.05f, 0.05f, 0.1f, 0.9f}));
    dl.pop_layer();
}

// An effect layer over content both backends draw alike (a gradient
// rectangle, a path ring, a dot): what differs is the effect alone.
void effect_scene(render::DisplayList& dl, std::string_view name, std::vector<std::pair<std::string_view, float>> values,
                  render::BlendMode mode = render::BlendMode::normal, float opacity = 1) {
    const geom::Rect layer{24.5f, 18, 110, 84};
    dl.fill_rect({0, 0, 160, 120}, geom::Color::hex(0xf1f5f9));
    dl.fill_rect({0, 60, 160, 60}, geom::Color{0.2f, 0.4f, 0.9f, 0.5f});
    const auto ref = render::effects().find(name);
    check(ref.has_value(), std::format("the effect {}", name));
    if (!ref) return;
    const auto params = render::effects().params(*ref, values);
    dl.push_layer(opacity, mode, *ref, params, layer);
    render::Paint card;
    card.kind = render::Paint::Kind::gradient;
    card.gradient.start = {layer.x, layer.y};
    card.gradient.end = {layer.right(), layer.bottom()};
    static const std::array<render::GradientStop, 3> stops{
        {{0, geom::Color::hex(0x4f46e5)}, {0.5f, geom::Color::hex(0xdb2777)}, {1, geom::Color{0.96f, 0.62f, 0.04f, 0.8f}}}};
    card.gradient.stops = stops;
    dl.fill_rect(layer, card);
    render::Path ring;
    ring.add_circle({layer.x + 55, layer.y + 42}, 26).add_circle({layer.x + 55, layer.y + 42}, 18);
    dl.fill_path(ring, render::FillRule::even_odd, render::Paint::solid(geom::Color::white()));
    render::Path dot;
    dot.add_circle({layer.x + 55, layer.y + 42}, 7);
    dl.fill_path(dot, render::FillRule::nonzero, render::Paint::solid(geom::Color::hex(0x0f172a)));
    dl.pop_layer();
}

// cpu_test's lit scene: a bevelled disc's picture and normal map, six lit
// copies (upright, turned 90°, mirrored; turned 30°, a mask of ink, smooth).
void lit_meshes(render::DisplayList& dl, Rig& r, geom::Color key = {1, 1, 1, 1}) {
    const raster::Rgba8 base{210, 160, 130, 255};
    omui::imageio::Image disc;
    disc.width = disc.height = 48;
    disc.rgba.assign(48 * 48 * 4, 0);
    std::vector<raster::Rgba8> px(48 * 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            const int dx = 2 * x + 1 - 48, dy = 2 * y + 1 - 48;
            if (dx * dx + dy * dy > 40 * 40) continue;
            px[static_cast<std::size_t>(y * 48 + x)] = base;
            std::uint8_t* p = disc.rgba.data() + (y * 48 + x) * 4;
            p[0] = base.r, p[1] = base.g, p[2] = base.b, p[3] = 255;
        }
    const render::ImageRef picture = r.images.add(48, 48, px);
    const omui::imageio::Image n = omui::imageio::normal_map(disc, {.bevel = 12, .smooth = 2});
    std::vector<raster::Rgba8> npx(48 * 48);
    std::memcpy(npx.data(), n.rgba.data(), n.rgba.size());
    // A list keeps its own copy of a material.
    render::Material material, ink, smooth;
    material = {.normal = r.images.add(48, 48, std::move(npx)), .light = {-1, -1}, .light_z = 0.8f, .shadow = geom::Color::hex(0x5b4a8a),
                .shadow_strength = 0.55f, .bands = 3, .softness = 0.06f, .rim = geom::Color{1, 0.95f, 0.85f, 1}, .rim_strength = 0.9f,
                .rim_width = 0.45f, .key = key};
    ink = material;
    ink.shadow_strength = 0;
    ink.rim = geom::Color{0.75f, 0.9f, 1, 1};
    ink.rim_strength = 1;
    smooth = material;
    smooth.bands = 0;
    const std::array<geom::Point, 4> uv{{{0, 0}, {48, 0}, {48, 48}, {0, 48}}};
    const std::array<std::uint16_t, 6> tri{0, 1, 2, 0, 2, 3};
    auto placed = [&](const geom::Affine& m) {
        std::array<geom::Point, 4> at{};
        for (std::size_t i = 0; i < 4; ++i) at[i] = m.apply(uv[i]);
        return at;
    };
    const geom::Affine centred = geom::Affine::translate(-24, -24);
    const auto mesh = [&](const geom::Affine& m, const render::Material& mat) {
        dl.draw_mesh(picture, placed(m * centred), uv, tri, render::Sampling::linear, 1, 0.5f, &mat);
    };
    mesh(geom::Affine::translate(32, 32), material);
    // Unlit draws between lit ones: the pipelines switch and switch back.
    dl.fill_rect({56, 4, 48, 8}, geom::Color{0.2f, 0.5f, 0.9f, 0.6f});
    dl.draw_mesh_mask(picture, placed(geom::Affine::translate(80, 60) * geom::Affine::scale(0.5f, 0.5f) * centred), uv, tri,
                      geom::Color{0.9f, 0.2f, 0.3f, 0.8f});
    mesh(geom::Affine::translate(80, 32) * geom::Affine::rotation(0, 1), material);
    mesh(geom::Affine::translate(128, 32) * geom::Affine::scale(-1, 1), material);
    mesh(geom::Affine::translate(32, 88) * render::rotate(0.5235988f), material);
    dl.draw_mesh_mask(picture, placed(geom::Affine::translate(80, 88) * centred), uv, tri, geom::Color{0.04f, 0.04f, 0.07f, 1},
                      render::Sampling::linear, 0.5f, &ink);
    mesh(geom::Affine::translate(128, 88), smooth);
}

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
         {1, 0}},
        {"rounded",
         [](render::DisplayList& dl, Rig&) {
             dl.fill_round_rect({10, 10, 70, 50}, geom::CornerRadii::all(12), render::Paint::solid(geom::Color::hex(0x0ea5e9)));
             dl.fill_round_rect({60.5f, 40.25f, 80, 60}, {4, 20, 0, 30}, render::Paint::solid(geom::Color{0.95f, 0.6f, 0.1f, 0.7f}));
             dl.fill_round_rect({100, 8, 50, 30}, geom::CornerRadii::all(40), render::Paint::solid(geom::Color::hex(0x16a34a)));
         },
         {8, 240}},
        // A rounded rectangle's stroke: the area between its outer and inner
        // rrects (render's stroke_ring), the GPU's exact, the CPU's arcs as
        // chords — two arcs a corner, so twice the edge pixels of a fill.
        {"rounded strokes",
         [](render::DisplayList& dl, Rig&) {
             dl.stroke_round_rect({10.5f, 10.5f, 60, 40}, geom::CornerRadii::all(8), 3, render::Paint::solid(geom::Color::hex(0x7c3aed)));
             dl.stroke_round_rect({90, 10, 50, 30}, geom::CornerRadii::all(3), 10, render::Paint::solid(geom::Color{0.86f, 0.15f, 0.15f, 0.8f}));
             dl.stroke_round_rect({20.25f, 64.75f, 120, 44}, {14, 0, 22, 4}, 1.5f, render::Paint::solid(geom::Color::hex(0x0f766e)));
         },
         {8, 480}},
        {"strokes",
         [](render::DisplayList& dl, Rig&) {
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
         {1, 0}},
        {"blend modes",
         [](render::DisplayList& dl, Rig&) {
             // A backdrop with opaque, translucent and empty parts, and every
             // mode over it in a cell: whole, under a fractional rectangle
             // clip, and under a rounded (mask) clip. Colour dodge and burn
             // divide by the source, so a last bit a GPU's division rounds
             // differently can grow to two levels.
             dl.fill_rect({0, 0, 160, 80}, gradient(render::GradientKind::linear, {0, 0}, {160, 0}, 0, render::Spread::pad, rainbow));
             dl.fill_rect({0, 50, 160, 40}, geom::Color{0.95f, 0.85f, 0.2f, 0.5f});
             for (int mode = 0; mode <= static_cast<int>(render::BlendMode::erase); ++mode) {
                 const float x = static_cast<float>(mode % 5) * 32, y = static_cast<float>(mode / 5) * 40;
                 const int clip = mode % 3;
                 if (clip == 1) dl.push_clip({x + 2.5f, y + 3.25f, 26.5f, 33.5f});
                 if (clip == 2) dl.push_clip({x + 1, y + 2, 30, 36}, geom::CornerRadii::all(9));
                 dl.push_layer(mode % 2 ? 0.8f : 1.0f, static_cast<render::BlendMode>(mode));
                 dl.fill_rect({x, y, 32, 40}, gradient(render::GradientKind::linear, {x, y}, {x + 32, y + 40}, 0, render::Spread::pad, rainbow));
                 // Shapes both backends cover alike (a fractional rectangle, a path),
                 // so what differs is the blending alone.
                 dl.fill_rect({x + 4.5f, y + 6.25f, 14, 20.5f}, render::Paint::solid(geom::Color{0.1f, 0.2f, 0.8f, 0.7f}));
                 render::Path dot;
                 dot.add_circle({x + 20, y + 26}, 9);
                 dl.fill_path(dot, render::FillRule::nonzero, render::Paint::solid(geom::Color{0.9f, 0.9f, 0.95f, 0.85f}));
                 dl.pop_layer();
                 if (clip != 0) dl.pop_clip();
             }
         },
         {2, 8}},
        {"blend arithmetic",
         [](render::DisplayList& dl, Rig&) {
             // Layers whose pixels both backends make alike (pixel-aligned solid
             // rectangles), over a backdrop of the same kind with alphas from 0
             // to 1: the blend shader against composite_pixel, at
             // two opacities and under a rounded mask.
             // Channels of 0 or 1 under alphas of n/255: premultiplied, the
             // same bytes however a backend rounds.
             auto exact = [](int bits, int alpha) {
                 return geom::Color{static_cast<float>(bits & 1), static_cast<float>((bits >> 1) & 1), static_cast<float>((bits >> 2) & 1),
                                    static_cast<float>(alpha) / 255.0f};
             };
             for (int band = 0; band < 6; ++band)
                 dl.fill_rect({0, static_cast<float>(band * 20), 160, 20}, exact(band + 1, band * 51));
             for (int mode = 1; mode <= static_cast<int>(render::BlendMode::erase); ++mode) {
                 const float x = static_cast<float>((mode - 1) % 7) * 22 + 2, y = static_cast<float>((mode - 1) / 7) * 60;
                 if (mode % 4 == 0) dl.push_clip({x, y + 4, 20, 52}, geom::CornerRadii::all(8));
                 dl.push_layer(mode % 2 ? 0.6f : 1.0f, static_cast<render::BlendMode>(mode));
                 for (int k = 0; k < 4; ++k)
                     dl.fill_rect({x, y + static_cast<float>(k * 15), 20, 15},
                                  exact(7 - k - mode % 3, 64 + k * 63));
                 dl.pop_layer();
                 if (mode % 4 == 0) dl.pop_clip();
             }
         },
         {1, 0}},
        {"effect dissolve", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "dissolve", {{"progress", 0.45f}, {"seed", 7}, {"grain", 3}}); }, {1, 0}},
        {"effect wipe", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "wipe", {{"progress", 0.4f}, {"angle", 30}, {"softness", 0.2f}}); }, {1, 0}},
        {"effect reveal", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "reveal", {{"progress", 0.6f}, {"x", 0.3f}, {"y", 0.6f}}); }, {1, 0}},
        {"effect displace", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "displace", {{"amount", 6}, {"wavelength", 30}, {"phase", 0.2f}}); }, {1, 0}},
        {"effect noise", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "noise", {{"amount", 0.3f}, {"seed", 3}}); }, {1, 0}},
        {"effect chromatic", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "chromatic", {{"offset", 3}, {"angle", 20}}); }, {1, 0}},
        {"effect blur", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "blur", {{"radius", 9}}); }, {1, 0}},
        {"effect pixelate", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "pixelate", {{"size", 7}}); }, {1, 0}},
        {"effect particles", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "particles", {{"progress", 0.35f}, {"seed", 5}, {"size", 4}}); }, {1, 0}},
        {"effect neon", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "neon", {{"radius", 10}, {"intensity", 1.2f}, {"sweep", 0.35f}}); }, {1, 0}},
        {"effect crt", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "crt", {{"curvature", 0.12f}, {"scanlines", 0.6f}, {"mask", 0.4f}}); }, {1, 0}},
        {"effect paper", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "paper", {{"grain", 0.6f}, {"warmth", 0.8f}, {"grain_size", 2}}); }, {1, 0}},
        {"effect tone", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "tone", {{"color_r", 0.85f}, {"color_g", 0.81f}, {"color_b", 0.74f}, {"amount", 0.8f}}); }, {1, 0}},
        {"effect wobble", [](render::DisplayList& dl, Rig&) { effect_scene(dl, "wobble", {{"amount", 3}, {"size", 14}, {"ink", 0.8f}}); }, {1, 0}},
        {"effect metaball",
         [](render::DisplayList& dl, Rig&) {
             dl.fill_rect({0, 0, 160, 120}, geom::Color::hex(0x0f172a));
             const std::vector<render::EffectShape> shapes{{{20.5f, 40, 70, 40}, geom::CornerRadii::all(20)}, {{100.5f, 44, 32, 32}, geom::CornerRadii::all(16)}};
             const auto ref = render::effects().find("metaball");
             const auto params = render::effects().params(*ref, std::vector<std::pair<std::string_view, float>>{{"smoothness", 24}});
             dl.push_layer(1, render::BlendMode::normal, *ref, params, {10.5f, 30, 140, 60}, shapes);
             for (const auto& sh : shapes) dl.fill_round_rect(sh.rect, sh.radii, render::Paint::solid(geom::Color::hex(0xe0e7ff)));
             dl.pop_layer();
         },
         // The content is two rounded rectangles: their edges as the rounded rectangles scene allows.
         {8, 240}},
        {"effect under a clip and a blend mode",
         [](render::DisplayList& dl, Rig&) {
             dl.push_clip({10, 10, 140, 100}, geom::CornerRadii::all(16));
             effect_scene(dl, "reveal", {{"progress", 0.7f}}, render::BlendMode::multiply, 0.8f);
             dl.pop_clip();
         },
         {1, 0}},
        {"liquid glass", [](render::DisplayList& dl, Rig&) { glass_scene(dl, {}); }, {2, 40}},
        {"liquid glass, frosted with dispersion",
         [](render::DisplayList& dl, Rig&) { glass_scene(dl, {{"blur", 6}, {"dispersion", 0.8f}, {"tint_a", 0.25f}, {"thickness", 18}}); },
         {2, 40}},
        {"liquid glass, a group merging",
         [](render::DisplayList& dl, Rig&) {
             glass_scene(dl, {{"smoothness", 20}, {"ior", 1.6f}},
                         {{{30.5f, 30, 52, 52}, geom::CornerRadii::all(26)}, {{90, 40, 44, 44}, geom::CornerRadii::all(22)}});
         },
         {2, 40}},
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
        {"mesh",
         [](render::DisplayList& dl, Rig& r) {
             std::vector<raster::Rgba8> px(16 * 16);
             for (int y = 0; y < 16; ++y)
                 for (int x = 0; x < 16; ++x) px[static_cast<std::size_t>(y * 16 + x)] = {static_cast<std::uint8_t>(x * 16), static_cast<std::uint8_t>(y * 16), 200, 255};
             const render::ImageRef img = r.images.add(16, 16, std::move(px));
             // A strip bent like an arm at its elbow.
             std::array<geom::Point, 6> uv{{{0, 0}, {8, 0}, {16, 0}, {0, 16}, {8, 16}, {16, 16}}};
             std::array<geom::Point, 6> at{{{10, 20}, {60, 30}, {100, 70}, {10, 60}, {50, 70}, {80, 100}}};
             std::array<std::uint16_t, 12> tri{0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4};
             dl.draw_mesh(img, at, uv, tri);
         },
         {2, 60}},
        // Lit meshes (Material): the shading's float arithmetic is the CPU's, step
        // for step, but a GPU's division and square root need not round as
        // IEEE does, so a channel may land a level apart.
        {"lit meshes", [](render::DisplayList& dl, Rig& r) { lit_meshes(dl, r); }, {2, 40}},
        // The same in a coloured light (a stage's sunset): the lit side warmed, the shade its own.
        {"lit meshes, a coloured light", [](render::DisplayList& dl, Rig& r) { lit_meshes(dl, r, geom::Color::hex(0xffb070)); }, {2, 40}},
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
        Rig rig{*device, std::move(*gpu), {}, *target, {}, {}, {}};
        for (const Case& c : cases()) {
            render::DisplayList dl;
            c.build(dl, rig);
            const std::string problem = rig.compare(dl, c.name, c.tolerance);
            check(problem.empty(), problem);
        }
    });

    s.test("masks are kept from frame to frame; the atlas grows, and past its size is emptied — every frame the CPU's pixels", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H,
                                                      rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "test.target"});
        if (!target) return;
        Rig rig{*device, std::move(*gpu), {}, *target, {}, {}, {}};
        // n circles, each a mask of its own (no two alike: the centre's
        // fraction and the radius differ), faint so all show.
        auto circles = [](render::DisplayList& dl, int n, float seed) {
            for (int i = 0; i < n; ++i) {
                const float f = static_cast<float>(i);
                render::Path c;
                c.add_circle({80 + std::fmod(f * 0.37f + seed, 1.0f) - 0.5f + static_cast<float>(i % 7) - 3, 60 + std::fmod(f * 0.61f + seed, 1.0f) - 0.5f},
                             40 + std::fmod(f * 0.13f + seed, 9.0f));
                dl.fill_path(c, render::FillRule::nonzero, render::Paint::solid(geom::Color{0.1f, 0.3f, 0.8f, 0.02f}));
            }
        };
        render::DisplayList few;
        circles(few, 30, 0.1f);
        check(rig.compare(few, "masks: first", {1, 0}).empty());
        const geom::Color bg = geom::Color::hex(0xf8fafc);
        auto again = rig.gpu.render(few, *target, {&rig.images, &rig.glyphs}, {.scale = 1, .background = bg});
        check(again.has_value());
        if (again) {
            check_eq(again->masks_made, std::size_t{0});
            check_eq(again->masks_reused, std::size_t{30});
            // Only the frame's instances go up again.
            check_eq(again->uploaded, std::size_t{30} * 192);
        }
        // A frame of more masks than the atlas's first size holds (it grows),
        // then frames of others: past its free size it is emptied of what
        // earlier frames left, and still draws what the CPU draws.
        std::size_t emptied = 0;
        for (float seed : {0.3f, 0.55f, 0.8f}) {
            render::DisplayList many;
            circles(many, 1200, seed);
            const std::string problem = rig.compare(many, std::format("masks: 1200 ({})", seed), {1, 0});
            check(problem.empty(), problem);
            check_eq(rig.last.masks_made, std::size_t{1200});
            emptied += rig.last.atlas_emptied;
        }
        check_eq(emptied, std::size_t{3});  // each needs more than its free size, beside the last frame's
        auto last = rig.gpu.render(few, *target, {&rig.images, &rig.glyphs}, {.scale = 1, .background = bg});
        check(last.has_value());
        if (last) check(last->masks_made + last->masks_reused == 30, "the first frame's masks, made again or kept");
    });

    s.test("a frame is the render graph's (D-81): each pass that runs a trace span, the same passes frame after frame", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 64, 48, rhi::TextureUsage::color_target, "test.graph"});
        if (!target) return;
        render::DisplayList dl;
        dl.push_layer(0.5f);
        dl.fill_rect({4, 4, 40, 30}, geom::Color::hex(0xdc2626));
        dl.push_layer(0.6f, render::BlendMode::multiply);
        dl.fill_rect({20, 16, 40, 30}, geom::Color::hex(0x2563eb));
        dl.pop_layer();
        dl.pop_layer();
        omui::trace::start();
        auto first = gpu->render(dl, *target, {});
        const auto t = omui::trace::stop();
        check(first.has_value());
        if (!first) return;
        // The target's pass, each layer's, the multiply's composite, and the target and outer layer again after them.
        check(first->gpu_passes >= 4 && first->gpu_passes_culled == 0, std::format("{} passes, {} left out", first->gpu_passes, first->gpu_passes_culled));
        std::size_t spans = 0;
        bool blend = false;
        for (const auto& e : t.events)
            if (e.kind == omui::trace::Kind::begin && std::string_view(e.category) == "render") {
                ++spans;
                blend = blend || std::string_view(e.name) == "render.vector.blend";
            }
        check(spans == first->gpu_passes && blend, std::format("{} spans of {} passes; the multiply composited", spans, first->gpu_passes));
        auto second = gpu->render(dl, *target, {});
        check(second && second->gpu_passes == first->gpu_passes, "the same frame again, the same passes");
    });

    s.test("what the GPU does not draw yet is counted, as the CPU counts what it skips", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 8, 8, rhi::TextureUsage::color_target, "test.small"});
        if (!target) return;
        render::DisplayList dl;
        dl.external_texture(7, {0, 0, 4, 4});
        auto report = gpu->render(dl, *target, {});
        check(report.has_value());
        if (report) check(report->external_skipped == 1 && report->commands == dl.size());
    });

    s.test("an image of the store is uploaded once, whatever frames draw it, and released when the store drops it", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H, rhi::TextureUsage::color_target, "test.target"});
        if (!gpu || !target) return;
        render::ImageStore images;
        const render::ImageRef big = images.add(256, 256, std::vector<raster::Rgba8>(256 * 256, raster::Rgba8{30, 60, 90, 255}));
        render::DisplayList dl;
        dl.draw_image(big, {0, 0, 256, 256}, {0, 0, 160, 120});
        auto first = gpu->render(dl, *target, {&images, nullptr});
        auto second = gpu->render(dl, *target, {&images, nullptr});
        check(first && second, "both frames drew");
        if (!first || !second) return;
        check(first->uploaded >= 256u * 256 * 4, std::format("the first frame uploads the image ({} bytes)", first->uploaded));
        check(second->uploaded < 256u * 256, std::format("the second does not ({} bytes)", second->uploaded));
        images.remove(big);
        render::DisplayList empty;
        check(gpu->render(empty, *target, {&images, nullptr}).has_value(), "a frame after the store dropped it");
    });

    s.test("an external texture the program made on the device is drawn like an image of its pixels", [] {
        auto device = open_device();
        if (!device) return;
        auto gpu = render::GpuRenderer::create(*device);
        if (!gpu) return;
        auto target = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, W, H,
                                                      rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "test.target"});
        // A "video frame": a texture uploaded by the program, premultiplied.
        constexpr std::uint32_t fw = 12, fh = 9;
        std::vector<raster::Rgba8> px(fw * fh);
        for (std::uint32_t y = 0; y < fh; ++y)
            for (std::uint32_t x = 0; x < fw; ++x)
                px[y * fw + x] = {static_cast<std::uint8_t>(x * 20), static_cast<std::uint8_t>(y * 28), 90, 255};
        auto frame = device->create(rhi::TextureDesc{rhi::Format::rgba8_unorm, fw, fh, rhi::TextureUsage::sampled, "test.video"});
        check(target.has_value() && frame.has_value());
        if (!target || !frame) return;
        rhi::CommandList up;
        up.upload(*frame, rhi::Region{0, 0, fw, fh}, std::as_bytes(std::span(px)));
        check(device->submit(up).has_value());
        render::DisplayList dl;
        dl.fill_rect({0, 0, W, H}, geom::Color::white());
        dl.external_texture(42, {20.5f, 10, 96, 72}, 0.9f);
        render::GpuOptions o;
        o.background = geom::Color::white();
        o.external = [&](std::uint64_t id) -> std::optional<rhi::Texture> { return id == 42 ? std::optional(*frame) : std::nullopt; };
        auto report = gpu->render(dl, *target, {}, o);
        check(report.has_value() && report->external_skipped == 0);
        auto got = device->read(*target, rhi::Region{0, 0, W, H});
        // The CPU's draw_image of the same pixels, bilinear, is the reference.
        render::ImageStore images;
        const render::ImageRef ref = images.add(static_cast<int>(fw), static_cast<int>(fh), std::vector<raster::Rgba8>(px));
        render::DisplayList same;
        same.fill_rect({0, 0, W, H}, geom::Color::white());
        same.draw_image(ref, {0, 0, fw, fh}, {20.5f, 10, 96, 72}, render::Sampling::linear, 0.9f);
        raster::Surface cpu(static_cast<int>(W), static_cast<int>(H));
        render::CpuRenderer r;
        (void)r.render(same, cpu, {&images, nullptr}, {.scale = 1, .background = geom::Color::white()});
        check(got.has_value());
        if (!got) return;
        int worst = 0;
        for (std::size_t i = 0; i < got->size(); ++i)
            worst = std::max(worst, std::abs(std::to_integer<int>((*got)[i]) - static_cast<int>(cpu.bytes()[i])));
        check(worst <= 1, std::format("the external texture is {} levels from the CPU's image of its pixels", worst));
    });

    return s.run(argc, argv);
}
