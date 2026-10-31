// The CPU backend renders a display list exactly as the raster canvas would,
// at any scale, and records which node drew each pixel.
import std;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {

// A picture of a disc (48 × 48, radius 20, one warm colour), its normal map
// (a rounded bevel) and six lit copies of it as meshes, light from the upper
// left: upright, turned 90°, mirrored (top row, centres 32, 80, 128 at
// y 32); turned 30°, as a mask of dark ink, smooth (bands 0) (bottom row,
// y 88). gpu_test draws the same.
struct LitScene {
    render::DisplayList        list;
    render::ImageRef           picture, normal;
    render::Material           material;
    raster::Rgba8              base;
    std::vector<geom::Point>   uv, at;
    std::vector<std::uint16_t> tri;
};

LitScene lit_scene(render::ImageStore& images) {
    LitScene s;
    s.base = {210, 160, 130, 255};
    omui::imageio::Image disc;
    disc.width = disc.height = 48;
    disc.alpha = omui::imageio::Alpha::premultiplied;
    disc.rgba.assign(48 * 48 * 4, 0);
    std::vector<raster::Rgba8> px(48 * 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
            const int dx = 2 * x + 1 - 48, dy = 2 * y + 1 - 48;
            if (dx * dx + dy * dy > 40 * 40) continue;
            px[static_cast<std::size_t>(y * 48 + x)] = s.base;
            std::uint8_t* p = disc.rgba.data() + (y * 48 + x) * 4;
            p[0] = s.base.r, p[1] = s.base.g, p[2] = s.base.b, p[3] = 255;
        }
    s.picture = images.add(48, 48, px);
    const omui::imageio::Image n = omui::imageio::normal_map(disc, {.bevel = 12, .smooth = 2});
    std::vector<raster::Rgba8> npx(48 * 48);
    std::memcpy(npx.data(), n.rgba.data(), n.rgba.size());
    s.normal = images.add(48, 48, std::move(npx));
    s.material = {.normal = s.normal, .light = {-1, -1}, .light_z = 0.8f, .shadow = geom::Color::hex(0x5b4a8a), .shadow_strength = 0.55f,
                  .bands = 3, .softness = 0.06f, .rim = geom::Color{1, 0.95f, 0.85f, 1}, .rim_strength = 0.9f, .rim_width = 0.45f};
    s.uv = {{0, 0}, {48, 0}, {48, 48}, {0, 48}};
    s.tri = {0, 1, 2, 0, 2, 3};
    auto placed = [&](const geom::Affine& m) {
        std::vector<geom::Point> at;
        for (const geom::Point& p : s.uv) at.push_back(m.apply(p));
        return at;
    };
    const geom::Affine centred = geom::Affine::translate(-24, -24);
    s.at = placed(geom::Affine::translate(32, 32) * centred);
    s.list.draw_mesh(s.picture, s.at, s.uv, s.tri, render::Sampling::linear, 1, 0.5f, &s.material);
    s.list.draw_mesh(s.picture, placed(geom::Affine::translate(80, 32) * geom::Affine::rotation(0, 1) * centred), s.uv, s.tri,
                     render::Sampling::linear, 1, 0.5f, &s.material);
    s.list.draw_mesh(s.picture, placed(geom::Affine::translate(128, 32) * geom::Affine::scale(-1, 1) * centred), s.uv, s.tri,
                     render::Sampling::linear, 1, 0.5f, &s.material);
    s.list.draw_mesh(s.picture, placed(geom::Affine::translate(32, 88) * render::rotate(0.5235988f) * centred), s.uv, s.tri,
                     render::Sampling::linear, 1, 0.5f, &s.material);
    render::Material ink = s.material;
    ink.shadow_strength = 0;
    ink.rim = geom::Color{0.75f, 0.9f, 1, 1};
    ink.rim_strength = 1;
    s.list.draw_mesh_mask(s.picture, placed(geom::Affine::translate(80, 88) * centred), s.uv, s.tri, geom::Color{0.04f, 0.04f, 0.07f, 1},
                          render::Sampling::linear, 0.5f, &ink);
    render::Material smooth = s.material;
    smooth.bands = 0;
    s.list.draw_mesh(s.picture, placed(geom::Affine::translate(128, 88) * centred), s.uv, s.tri, render::Sampling::linear, 1, 0.5f, &smooth);
    return s;
}

void card(render::DisplayList& dl) {
    dl.begin_node(1);
    dl.box_shadow({20, 20, 160, 100}, geom::CornerRadii::all(12), {0, 4}, 16, 0, {0, 0, 0, 0.25f});
    dl.fill_round_rect({20, 20, 160, 100}, geom::CornerRadii::all(12), render::Paint::solid(geom::Color::white()));
    dl.end_node();
    dl.begin_node(2);
    std::array<render::GradientStop, 2> stops{{{0, geom::Color::hex(0x6366f1)}, {1, geom::Color::hex(0xec4899)}}};
    render::Paint g;
    g.kind = render::Paint::Kind::gradient;
    g.gradient.start = {40, 40};
    g.gradient.end = {160, 40};
    g.gradient.stops = stops;
    dl.fill_round_rect({40, 40, 120, 32}, geom::CornerRadii::all(16), g);
    dl.end_node();
    dl.begin_node(3);
    dl.push_transform(geom::Affine::translate(100, 96));
    render::Path check;
    check.move_to({-10, 0}).line_to({-3, 7}).line_to({10, -6});
    dl.stroke_path(check, {.width = 3, .cap = render::LineCap::round, .join = render::LineJoin::round},
                   render::Paint::solid(geom::Color::hex(0x10b981)));
    dl.pop_transform();
    dl.end_node();
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"render.cpu"};

    s.test("a card at 1x and 2x, with provenance", [] {
        render::DisplayList dl;
        card(dl);
        check(dl.balanced());
        render::CpuRenderer cpu;
        render::ImageStore images;
        raster::Surface one(200, 140), two(400, 280);
        auto report = cpu.render(dl, one, {&images, nullptr}, {.scale = 1, .object_ids = true, .background = geom::Color::hex(0xf1f5f9)});
        check_eq(report.commands, dl.size());
        check_eq(cpu.object_at(100, 56, 200), std::uint32_t{2}, std::source_location::current());
        check_eq(cpu.object_at(30, 110, 200), std::uint32_t{1});
        check_eq(cpu.object_at(5, 5, 200), std::uint32_t{0});
        cpu.render(dl, two, {&images, nullptr}, {.scale = 2, .background = geom::Color::hex(0xf1f5f9)});
        // The same pixel at 2x is the same colour where the colour is flat.
        check(one.at(30, 110) == two.at(60, 220));
        auto dir = scratch_dir("render-cpu");
        omui::imageio::View v1{reinterpret_cast<const std::uint8_t*>(one.pixels().data()), 200, 140, 800};
        omui::imageio::View v2{reinterpret_cast<const std::uint8_t*>(two.pixels().data()), 400, 280, 1600};
        check(omui::imageio::save_png(dir / "card@1x.png", v1).has_value());
        check(omui::imageio::save_png(dir / "card@2x.png", v2).has_value());
        std::println("    {:016x} {:016x}  ({})", one.hash(), two.hash(), (dir / "card@2x.png").generic_string());
        check_eq(std::format("{:016x}/{:016x}", one.hash(), two.hash()), std::string("76f0acb68946e096/09f8ffc1b651f38d"));
    });

    s.test("images by reference; missing ones are reported", [] {
        render::ImageStore images;
        std::vector<raster::Rgba8> px{{255, 0, 0, 255}, {0, 0, 255, 255}};
        auto ref = images.add(2, 1, px);
        render::DisplayList dl;
        dl.draw_image(ref, {0, 0, 2, 1}, {0, 0, 4, 2}, render::Sampling::nearest);
        dl.draw_image({999}, {0, 0, 1, 1}, {0, 0, 1, 1});
        raster::Surface out(4, 2);
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, out, {&images, nullptr});
        check(out.at(0, 0) == px[0] && out.at(3, 1) == px[1]);
        check_eq(report.missing_images, std::size_t{1});
    });

    s.test("a textured mesh: a picture bent by its vertices — flat, it is the image drawn; bent, the same bytes every run", [] {
        render::ImageStore images;
        std::vector<raster::Rgba8> px(32 * 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                px[static_cast<std::size_t>(y * 32 + x)] = {static_cast<std::uint8_t>(x * 8), static_cast<std::uint8_t>(y * 8), 160, 255};
        const auto ref = images.add(32, 32, px);
        // A 3 × 3 grid of vertices over the image, drawn at twice its size.
        std::vector<geom::Point> uv, at;
        std::vector<std::uint16_t> tri;
        for (int j = 0; j <= 2; ++j)
            for (int i = 0; i <= 2; ++i) {
                uv.push_back({16.0f * i, 16.0f * j});
                at.push_back({8 + 32.0f * i, 8 + 32.0f * j});
            }
        for (std::uint16_t j = 0; j < 2; ++j)
            for (std::uint16_t i = 0; i < 2; ++i) {
                const std::uint16_t k = static_cast<std::uint16_t>(j * 3 + i);
                for (std::uint16_t v : {k, static_cast<std::uint16_t>(k + 1), static_cast<std::uint16_t>(k + 4), k, static_cast<std::uint16_t>(k + 4), static_cast<std::uint16_t>(k + 3)})
                    tri.push_back(v);
            }
        render::CpuRenderer cpu;
        render::DisplayList flat, image;
        flat.draw_mesh(ref, at, uv, tri, render::Sampling::nearest);
        image.draw_image(ref, {0, 0, 32, 32}, {8, 8, 64, 64}, render::Sampling::nearest);
        raster::Surface a(80, 80), b(80, 80);
        cpu.render(flat, a, {&images, nullptr});
        cpu.render(image, b, {&images, nullptr});
        int differ = 0;
        for (int y = 10; y < 70; ++y)
            for (int x = 10; x < 70; ++x) differ += !(a.at(x, y) == b.at(x, y));
        check_eq(differ, 0);  // inside, where no edge's antialiasing reaches: the image itself, no cracks along the diagonals
        // Bent: the middle row pushed right, as a bone would.
        for (int i = 3; i < 6; ++i) at[static_cast<std::size_t>(i)].x += 10;
        render::DisplayList bent;
        bent.draw_mesh(ref, at, uv, tri);
        raster::Surface c(90, 80);
        cpu.render(bent, c, {&images, nullptr});
        std::println("    bent {:016x}", c.hash());
        check(c.at(50, 40) != b.at(50, 40), "moved");
        check_eq(bent.hash(), [&] {
            render::DisplayList again;
            again.draw_mesh(ref, at, uv, tri);
            return again.hash();
        }());
    });

    s.test("a long mesh turned and running out of its clip: drawn whole up to the clip, one quad or many", [] {
        render::ImageStore images;
        const auto ref = images.add(400, 200, std::vector<raster::Rgba8>(400 * 200, raster::Rgba8{200, 60, 40, 255}));
        // A strip of the picture (300 × 40 at 20, 100), turned −25° and scaled 0.6, its far end past the clip.
        auto strip = [&](int cols, float clip) {
            std::vector<geom::Point> uv, at;
            std::vector<std::uint16_t> tri;
            const float c = std::cos(-0.436f), s = std::sin(-0.436f);
            for (int j = 0; j <= 1; ++j)
                for (int i = 0; i <= cols; ++i) {
                    const float u = 20 + 300.0f * i / cols, v = 100 + 40.0f * j;
                    uv.push_back({u, v});
                    const float x = (u - 20) * 0.6f, y = (v - 100) * 0.6f;
                    at.push_back({20 + x * c - y * s, 120 - (x * s + y * c)});
                }
            for (int i = 0; i < cols; ++i) {
                const auto k = static_cast<std::uint16_t>(i), w = static_cast<std::uint16_t>(cols + 1);
                for (std::uint16_t v : {k, static_cast<std::uint16_t>(k + 1), static_cast<std::uint16_t>(k + w + 1), k, static_cast<std::uint16_t>(k + w + 1),
                                        static_cast<std::uint16_t>(k + w)})
                    tri.push_back(v);
            }
            render::DisplayList dl;
            dl.push_clip({0, 0, clip, 160});
            dl.draw_mesh(ref, at, uv, tri);
            dl.pop_clip();
            raster::Surface out(200, 160);
            render::CpuRenderer{}.render(dl, out, {&images, nullptr});
            int painted = 0;
            for (int y = 0; y < 160; ++y)
                for (int x = 0; x < 200; ++x) painted += out.at(x, y).a > 128;
            return painted;
        };
        const int whole = strip(1, 400), whole8 = strip(8, 400);
        const int one = strip(1, 120), many = strip(8, 120);
        // Eight quads cover a little more (their seams grown half a pixel); no more than that unclipped.
        check(one > 2000 && std::abs(many - one) <= whole8 - whole, std::format("{} pixels drawn by one quad, {} by eight", one, many));
    });

    s.test("lit meshes: a bevelled disc under a light from the upper left — bands, a rim on the lit edge; turned and mirrored copies lit by the light, not the picture", [] {
        render::ImageStore images;
        const LitScene scene = lit_scene(images);
        raster::Surface out(160, 120);
        render::CpuRenderer cpu;
        const auto report = cpu.render(scene.list, out, {&images, nullptr});
        check_eq(report.missing_images, std::size_t{0});
        const auto lum = [&](int x, int y) {
            const raster::Rgba8 p = out.at(x, y);
            return 2 * p.r + 5 * p.g + p.b;
        };
        // The upright copy (centre 32, 32): the upper left in the top band with the rim on it, the
        // middle (facing the viewer) in the middle band, the lower right in shadow.
        const int ul = lum(32 - 13, 32 - 13), mid = lum(32, 32), lr = lum(32 + 12, 32 + 12);
        check(ul > mid && mid > lr, std::format("upper left {}, middle {}, lower right {}", ul, mid, lr));
        // The brightest pixel toward the light, and toward the shadow, along the diagonal's outer part.
        const auto brightest = [&](int cx, int cy, int from, int to) {
            raster::Rgba8 best{};
            for (int t = from; t <= to; ++t)
                if (const raster::Rgba8 p = out.at(cx + t, cy + t); p.a == 255 && p.r + p.g + p.b > best.r + best.g + best.b) best = p;
            return best;
        };
        const raster::Rgba8 base = scene.base, centre = out.at(32, 32), lit_edge = brightest(32, 32, -15, -10), dark = out.at(32 + 12, 32 + 12);
        check(lit_edge.r > base.r + 20 && lit_edge.g > base.g + 20, std::format("the rim brightens the lit edge: {} {} {}", lit_edge.r, lit_edge.g, lit_edge.b));
        const raster::Rgba8 far_edge = brightest(32, 32, 10, 15);
        check(far_edge.r < base.r, "no rim away from the light");
        check(dark.r < centre.r && dark.b * 255 / dark.r > base.b * 255 / base.r, "the shadow is darker and toward its (purple) colour");
        // Three bands: along the diagonal the colour holds still between steps (plateaus).
        std::set<std::uint32_t> levels;
        for (int t = -8; t <= 8; ++t) {
            const raster::Rgba8 p = out.at(32 + t, 32 + t);
            levels.insert(static_cast<std::uint32_t>(p.r) << 16 | static_cast<std::uint32_t>(p.g) << 8 | p.b);
        }
        check(levels.size() <= 8, std::format("{} colours along 17 pixels of the diagonal: banded", levels.size()));
        // The copy turned 90° (centre 80, 32) and the mirrored copy (centre 128, 32) shade as the
        // upright one, pixel for pixel: the light stays where it is as the picture turns or flips.
        int turned = 0, mirrored = 0, opaque = 0;
        for (int dy = -22; dy < 22; ++dy)
            for (int dx = -22; dx < 22; ++dx) {
                const raster::Rgba8 a = out.at(32 + dx, 32 + dy), b = out.at(80 + dx, 32 + dy), c = out.at(128 + dx, 32 + dy);
                if (a.a != 255 || b.a != 255 || c.a != 255) continue;
                ++opaque;
                turned = std::max({turned, std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
                mirrored = std::max({mirrored, std::abs(a.r - c.r), std::abs(a.g - c.g), std::abs(a.b - c.b)});
            }
        check(opaque > 1000);
        check_eq(turned, 0);
        check_eq(mirrored, 0);
        // Turned 30° (centre 32, 88): still lit from the upper left.
        check(lum(32 - 13, 88 - 13) > lum(32 + 12, 88 + 12));
        // The picture as a mask (centre 80, 88): dark ink, its alpha the disc's, a rim of light on its upper left only.
        const raster::Rgba8 ink_lit = brightest(80, 88, -15, -10), ink_dark = brightest(80, 88, 10, 15), ink_mid = out.at(80, 88);
        check(ink_mid.a == 255 && ink_mid.r < 40 && ink_dark.r < 40 && ink_lit.r > 100, std::format("ink {} {} {}", ink_mid.r, ink_dark.r, ink_lit.r));
        check(out.at(80, 88 - 23).a == 0 && out.at(80 + 23, 88).a == 0, "outside the disc, nothing");
        // Smooth (bands 0, centre 128, 88): more distinct colours along the diagonal than banded.
        std::set<std::uint32_t> smooth;
        for (int t = -8; t <= 8; ++t) smooth.insert(out.at(128 + t, 88 + t).r);
        check(smooth.size() > levels.size(), std::format("smooth: {} levels", smooth.size()));
        // Premultiplied everywhere; the same bytes every run (pinned for every platform).
        for (const raster::Rgba8 p : out.pixels()) check(p.r <= p.a && p.g <= p.a && p.b <= p.a);
        raster::Surface again(160, 120);
        render::CpuRenderer{}.render(scene.list, again, {&images, nullptr});
        check(again.bytes().size() == out.bytes().size() && std::ranges::equal(again.bytes(), out.bytes()));
        std::println("    lit {:016x}", out.hash());
        const omui::imageio::View view{reinterpret_cast<const std::uint8_t*>(out.pixels().data()), 160, 120, 160 * 4};
        check(omui::imageio::save_png(scratch_dir("render-cpu") / "lit.png", view).has_value());  // to look at
        check_eq(out.hash(), 0x27807ca9fb71b7c2ull);
        // A list with a material hashes apart from the same without one; copied lists keep it.
        render::DisplayList plain, copy;
        plain.draw_mesh(scene.picture, scene.at, scene.uv, scene.tri);
        check(plain.hash() != scene.list.hash());
        copy.append(scene.list);
        check_eq(copy.hash(), scene.list.hash());
        // A missing normal map draws unlit, and is counted.
        render::Material lost = scene.material;
        lost.normal = {999};
        render::DisplayList missing, unlit;
        missing.draw_mesh(scene.picture, scene.at, scene.uv, scene.tri, render::Sampling::linear, 1, 0.5f, &lost);
        unlit.draw_mesh(scene.picture, scene.at, scene.uv, scene.tri);
        raster::Surface m1(160, 120), m2(160, 120);
        check_eq(cpu.render(missing, m1, {&images, nullptr}).missing_images, std::size_t{1 * 2});
        cpu.render(unlit, m2, {&images, nullptr});
        check(std::ranges::equal(m1.bytes(), m2.bytes()));
    });

    s.test("unregistered effects and external textures are skipped and counted (R-3)", [] {
        render::DisplayList dl;
        std::array<float, 1> params{0.5f};
        dl.push_layer(1, render::BlendMode::normal, {999}, params);  // no effect has this ref
        dl.fill_rect({0, 0, 4, 4}, geom::Color::white());
        dl.pop_layer();
        dl.external_texture(42, {0, 0, 2, 2});
        raster::Surface out(4, 4);
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, out, {});
        check_eq(report.effects_skipped, std::size_t{1});
        check_eq(report.external_skipped, std::size_t{1});
        check_eq(int(out.at(1, 1).a), 255, std::source_location::current());
    });

    return s.run(argc, argv);
}
