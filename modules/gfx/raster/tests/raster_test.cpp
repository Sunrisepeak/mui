// The rasterizer's coverage is exact where geometry is exact, compositing is
// exact 8-bit arithmetic, and a scene's bytes are pinned for every platform.
import std;
import openmui.geom;
import openmui.raster;
import hanim;
import openmui.testing;

using namespace omui::testing;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {

raster::Coverage fill(const raster::PathBuilder& pb, raster::FillRule rule = raster::FillRule::nonzero,
                      geom::PixelRect clip = {0, 0, 64, 64}) {
    raster::Coverage c;
    raster::fill_coverage(pb.data(), {}, rule, clip, c);
    return c;
}

std::uint64_t total(const raster::Coverage& c) {
    std::uint64_t t = 0;
    for (auto v : c.values) t += v;
    return t;
}

int at(const raster::Coverage& c, int x, int y) {
    if (x < c.bounds.x || y < c.bounds.y || x >= c.bounds.x + c.bounds.width || y >= c.bounds.y + c.bounds.height) return 0;
    return c.at(x, y);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"raster"};

    s.test("an integer square covers exactly its pixels", [] {
        raster::PathBuilder pb;
        pb.rect({4, 4, 10, 10});
        auto c = fill(pb);
        check_eq(at(c, 4, 4), 255);
        check_eq(at(c, 13, 13), 255);
        check_eq(at(c, 14, 13), 0);
        check_eq(at(c, 3, 8), 0);
        check_eq(total(c), std::uint64_t{100 * 255});
    });

    s.test("half-pixel edges are half covered", [] {
        raster::PathBuilder pb;
        pb.rect({4.5f, 4, 10, 10});
        auto c = fill(pb);
        check_eq(at(c, 4, 8), 128);
        check_eq(at(c, 14, 8), 128);
        check_eq(at(c, 9, 8), 255);
    });

    s.test("orientation does not matter; the fill rule does", [] {
        raster::PathBuilder cw, ccw;
        cw.move_to({2, 2}).line_to({30, 5}).line_to({10, 28}).close();
        ccw.move_to({2, 2}).line_to({10, 28}).line_to({30, 5}).close();
        check(fill(cw).values == fill(ccw).values);
        raster::PathBuilder rings;
        rings.rect({0, 0, 20, 20}).rect({5, 5, 10, 10});  // same direction
        auto nz = fill(rings, raster::FillRule::nonzero);
        auto eo = fill(rings, raster::FillRule::even_odd);
        check_eq(at(nz, 10, 10), 255);
        check_eq(at(eo, 10, 10), 0);
        check_eq(at(eo, 2, 2), 255);
    });

    s.test("a circle's coverage is its area", [] {
        raster::PathBuilder pb;
        float r = 20, k = 0.5522847498307936f * r, cx = 32, cy = 32;
        pb.move_to({cx + r, cy})
            .cubic_to({cx + r, cy + k}, {cx + k, cy + r}, {cx, cy + r})
            .cubic_to({cx - k, cy + r}, {cx - r, cy + k}, {cx - r, cy})
            .cubic_to({cx - r, cy - k}, {cx - k, cy - r}, {cx, cy - r})
            .cubic_to({cx + k, cy - r}, {cx + r, cy - k}, {cx + r, cy})
            .close();
        double area = static_cast<double>(total(fill(pb))) / 255.0;
        check_near(area, 3.14159265 * r * r, 3.14159265 * r * r * 0.002);
    });

    s.test("a shape crossing the clip keeps its winding", [] {
        raster::PathBuilder pb;
        pb.rect({-50, 10, 80, 10});
        auto c = fill(pb, raster::FillRule::nonzero, {0, 0, 64, 64});
        check_eq(at(c, 0, 15), 255);
        check_eq(at(c, 29, 15), 255);
        check_eq(at(c, 30, 15), 0);
        check_eq(c.bounds.x, 0);
    });

    s.test("source-over is exact", [] {
        raster::Surface surf(4, 4);
        surf.clear({0, 0, 255, 255});
        raster::Canvas canvas(surf);
        canvas.fill_rect({0, 0, 4, 4}, raster::Paint::solid({1, 0, 0, 0.5f}));
        // red 50% over blue: r = 128, b = 255·(1 − 128/255) rounded
        auto p = surf.at(1, 1);
        check_eq(int(p.r), 128);
        check_eq(int(p.g), 0);
        check_eq(int(p.b), 127);
        check_eq(int(p.a), 255);
    });

    s.test("gradients", [] {
        raster::Surface surf(256, 1);
        raster::Canvas canvas(surf);
        std::array<raster::Stop, 2> stops{{{0, geom::Color::black()}, {1, geom::Color::white()}}};
        raster::Paint p;
        p.kind = raster::Paint::Kind::gradient;
        p.start = {0, 0};
        p.end = {256, 0};
        p.stops = stops;
        canvas.fill_rect({0, 0, 256, 1}, p);
        check_eq(int(surf.at(0, 0).r), 0);
        check_eq(int(surf.at(128, 0).r), 128);
        check_eq(int(surf.at(255, 0).r), 255);  // sampled at the pixel centre, 255.5 / 256
        for (int x = 1; x < 256; ++x) check(surf.at(x, 0).r >= surf.at(x - 1, 0).r);
        p.spread = raster::Spread::reflect;
        p.end = {64, 0};
        surf.clear();
        canvas.fill_rect({0, 0, 256, 1}, p);
        check_eq(int(surf.at(32, 0).r), int(surf.at(95, 0).r));  // centres 32.5 and 95.5 mirror around 64
    });

    s.test("strokes: width, caps, joins without seams", [] {
        raster::PathBuilder line;
        line.move_to({10, 10}).line_to({30, 10});
        raster::Polygons polys;
        raster::stroke_outline(line.data(), {}, {.width = 2, .cap = raster::Cap::butt}, polys);
        raster::Coverage c;
        raster::Rasterizer r;
        r.rasterize(polys, raster::FillRule::nonzero, {0, 0, 64, 64}, c);
        check_eq(at(c, 15, 9), 255);
        check_eq(at(c, 15, 10), 255);
        check_eq(at(c, 15, 11), 0);
        check_eq(at(c, 9, 10), 0);
        polys.clear();
        raster::stroke_outline(line.data(), {}, {.width = 2, .cap = raster::Cap::square}, polys);
        r.rasterize(polys, raster::FillRule::nonzero, {0, 0, 64, 64}, c);
        check_eq(at(c, 9, 10), 255);
        check_eq(total(c), std::uint64_t{22 * 2 * 255});
        // A closed square: the outline's coverage equals ring area exactly.
        raster::PathBuilder sq;
        sq.rect({10, 10, 20, 20});
        polys.clear();
        raster::stroke_outline(sq.data(), {}, {.width = 2, .join = raster::Join::miter}, polys);
        r.rasterize(polys, raster::FillRule::nonzero, {0, 0, 64, 64}, c);
        check_eq(total(c), std::uint64_t{(22 * 22 - 18 * 18) * 255});
        // Dashes: on 4, off 4 along 20 points: three dashes.
        raster::PathBuilder dl;
        dl.move_to({0, 5}).line_to({20, 5});
        std::array<float, 2> dash{4, 4};
        polys.clear();
        raster::stroke_outline(dl.data(), {}, {.width = 2, .dashes = dash}, polys);
        r.rasterize(polys, raster::FillRule::nonzero, {0, 0, 64, 64}, c);
        check_eq(total(c), std::uint64_t{12 * 2 * 255});
    });

    s.test("clips", [] {
        raster::Surface surf(20, 20);
        raster::Canvas canvas(surf);
        canvas.push_clip_rect({5, 5, 10, 10});
        canvas.push_clip_rect({0, 0, 8, 20});
        canvas.fill_rect({0, 0, 20, 20}, raster::Paint::solid(geom::Color::white()));
        canvas.pop_clip();
        canvas.pop_clip();
        check_eq(int(surf.at(6, 6).a), 255);
        check_eq(int(surf.at(8, 6).a), 0);
        check_eq(int(surf.at(4, 6).a), 0);
        surf.clear();
        canvas.push_clip_rect({5.5f, 0, 10, 20});
        canvas.fill_rect({0, 0, 20, 20}, raster::Paint::solid(geom::Color::white()));
        canvas.pop_clip();
        check_eq(int(surf.at(5, 3).a), 128);
        check(canvas.depth() == 1);
    });

    s.test("layers: opacity and blend modes", [] {
        raster::Surface surf(2, 2);
        surf.clear({255, 255, 255, 255});
        raster::Canvas canvas(surf);
        canvas.push_layer(0.5f);
        canvas.fill_rect({0, 0, 2, 2}, raster::Paint::solid({1, 0, 0, 1}));
        canvas.fill_rect({0, 0, 1, 2}, raster::Paint::solid({0, 0, 1, 1}));
        canvas.pop_layer();
        check(surf.at(1, 0) == raster::Rgba8{255, 127, 127, 255});
        check(surf.at(0, 0) == raster::Rgba8{127, 127, 255, 255});
        surf.clear({128, 128, 128, 255});
        canvas.push_layer(1, raster::Blend::multiply);
        canvas.fill_rect({0, 0, 2, 2}, raster::Paint::solid({0.5f, 1, 0, 1}));
        canvas.pop_layer();
        auto p = surf.at(0, 0);
        check_eq(int(p.r), 64);
        check_eq(int(p.g), 128);
        check_eq(int(p.b), 0);
    });

    s.test("box shadows", [] {
        raster::Surface surf(80, 80);
        raster::Canvas canvas(surf);
        canvas.box_shadow({20, 20, 40, 40}, geom::CornerRadii::all(6), {0, 0}, 12, 0, geom::Color::black(), false);
        check_eq(int(surf.at(40, 40).a), 0);   // inside the box: not drawn
        int left = surf.at(15, 40).a, right = surf.at(64, 40).a;
        check_eq(left, right);
        check(left > 20 && left < 200, std::format("edge alpha {}", left));
        check(surf.at(2, 40).a < surf.at(15, 40).a);
        surf.clear();
        canvas.box_shadow({20, 20, 40, 40}, {}, {0, 0}, 8, 0, geom::Color::black(), true);
        check(surf.at(21, 40).a > surf.at(30, 40).a);
        check_eq(int(surf.at(10, 40).a), 0);
    });

    s.test("images and object ids", [] {
        std::array<raster::Rgba8, 4> pixels{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}}};
        raster::ImageView img{pixels.data(), 2, 2, 2};
        raster::Surface surf(4, 4);
        raster::Canvas canvas(surf);
        canvas.enable_object_ids();
        canvas.set_object_id(7);
        canvas.draw_image(img, {0, 0, 2, 2}, {0, 0, 4, 4}, raster::Sampling::nearest, 1);
        check(surf.at(0, 0) == pixels[0]);
        check(surf.at(3, 0) == pixels[1]);
        check(surf.at(0, 3) == pixels[2]);
        check(surf.at(3, 3) == pixels[3]);
        check_eq(canvas.object_ids()[5], std::uint32_t{7});
    });

    s.test("pinned scene", [] {
        raster::Surface surf(256, 256);
        surf.clear({250, 250, 252, 255});
        raster::Canvas canvas(surf);
        canvas.box_shadow({40, 40, 176, 120}, geom::CornerRadii::all(16), {0, 6}, 24, 2, {0, 0, 0, 0.3f}, false);
        canvas.fill_round_rect({40, 40, 176, 120}, geom::CornerRadii::all(16), raster::Paint::solid(geom::Color::white()));
        std::array<raster::Stop, 3> stops{{{0, geom::Color::hex(0x4f46e5)}, {0.5f, geom::Color::hex(0x06b6d4)}, {1, geom::Color::hex(0x22c55e)}}};
        raster::Paint g;
        g.kind = raster::Paint::Kind::gradient;
        g.gradient = raster::GradientKind::radial;
        g.start = {128, 100};
        g.radius = 60;
        g.stops = stops;
        canvas.push_clip_round_rect({56, 56, 144, 88}, geom::CornerRadii::all(10));
        canvas.fill_rect({0, 0, 256, 256}, g);
        canvas.pop_clip();
        raster::PathBuilder wave;
        wave.move_to({48, 200}).cubic_to({90, 160}, {150, 240}, {208, 196});
        canvas.stroke_path(wave.data(), {.width = 6, .cap = raster::Cap::round, .join = raster::Join::round},
                           raster::Paint::solid(geom::Color::hex(0xf97316)));
        raster::PathBuilder star;
        for (int i = 0; i < 10; ++i) {
            double a = i * 3.14159265358979 / 5 - 3.14159265358979 / 2;
            float rad = i % 2 ? 8.0f : 20.0f;
            geom::Point p{static_cast<float>(220 + rad * hanim::dm::cos(a)), static_cast<float>(36 + rad * hanim::dm::sin(a))};
            if (i == 0) star.move_to(p);
            else star.line_to(p);
        }
        star.close();
        canvas.push_layer(0.8f, raster::Blend::multiply);
        canvas.fill_path(star.data(), raster::FillRule::even_odd, raster::Paint::solid(geom::Color::hex(0xeab308)),
                         omui::geom::Affine::translate(-4, 2));
        canvas.pop_layer();
        std::println("    scene {:016x}", surf.hash());
        check_eq(std::format("{:016x}", surf.hash()), std::string("b42ebce16bf6eccf"));
    });

    return s.run(argc, argv);
}
