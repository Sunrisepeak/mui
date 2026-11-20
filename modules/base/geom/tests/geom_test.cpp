import std;
import mlibs.geom;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace geom = mlibs::geom;

static_assert(geom::snap(0.5f, 1) == 1 && geom::snap(-0.51f, 1) == -1);
static_assert(geom::Rect{0, 0, 10, 10}.intersects(geom::Rect{5, 5, 1, 1}));

UTest cases{"geom", [] {

    "rounding is half up, negatives included"_test = [] {
        expect(eq(geom::snap(0.49f, 1), 0));
        expect(eq(geom::snap(-0.5f, 1), 0));
        expect(eq(geom::snap(10.25f, 2), 21));
        expect(eq(geom::snap(1.0f / 3.0f, 3), 1));
    };

    "rectangles that share an edge share it at every scale"_test = [] {
        for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
            float x = 0;
            geom::PixelRect prev{};
            for (int i = 0; i < 200; ++i) {
                float w = 7.3f + static_cast<float>(i % 7) * 0.37f;
                auto r = geom::snap(geom::Rect{x, 0, w, 10}, scale);
                if (i > 0) expect(eq(r.x, prev.x + prev.width));
                prev = r;
                x += w;
            }
        }
    };

    "rectangles"_test = [] {
        geom::Rect a{0, 0, 10, 10}, b{5, 5, 10, 10};
        expect(a.intersection(b) == geom::Rect{5, 5, 5, 5});
        expect(a.united(b) == geom::Rect{0, 0, 15, 15});
        expect(a.contains(geom::Point{0, 0}) && !a.contains(geom::Point{10, 5}));
        expect(a.contains(geom::Rect{1, 1, 9, 9}));
        expect(!a.intersects(geom::Rect{10, 0, 5, 5})) << "touching is not intersecting";
        expect(a.inset(geom::Insets::all(2)) == geom::Rect{2, 2, 6, 6});
        expect(a.inset(geom::Insets::all(2)).outset(geom::Insets::all(2)) == a);
    };

    "radii fit their box"_test = [] {
        auto r = geom::CornerRadii::all(50).fitted({10, 20});
        expect(r == geom::CornerRadii::all(5));
        expect(geom::CornerRadii::all(3).fitted({100, 100}) == geom::CornerRadii::all(3));
    };

    "colours"_test = [] {
        auto c = geom::Color::parse("#ff800080");
        auto p = c->premultiplied8();
        expect(eq(int(p[0]), 128));
        expect(eq(int(p[1]), 64));
        expect(eq(int(p[3]), 128));
        expect(geom::Color::parse("#fff") == geom::Color::white());
        expect(!geom::Color::parse("#ffff0").has_value());
    };

    "affine transforms"_test = [] {
        auto m = geom::Affine::translate(10, 5) * geom::Affine::scale(2, 3);
        expect(m.apply({1, 1}) == geom::Point{12, 8});
        auto back = m.inverse()->apply({12, 8});
        expect(near(back.x, 1, 1e-6));
        expect(near(back.y, 1, 1e-6));
        expect(!geom::Affine::scale(0, 1).inverse().has_value());
        expect(near(geom::Affine::scale(3, 2).max_scale(), 3, 1e-5));
        expect(geom::Affine::translate(1, 1).map({0, 0, 2, 2}) == geom::Rect{1, 1, 2, 2});
    };

    "a closed contour's exact area and centroid, whatever its orientation"_test = [] {
        struct Cubic { mlibs::geom::Point start, control1, control2, end; };
        const auto line = [](mlibs::geom::Point from, mlibs::geom::Point to) {
            return Cubic{from, {from.x + (to.x - from.x) / 3, from.y + (to.y - from.y) / 3},
                         {from.x + 2 * (to.x - from.x) / 3, from.y + 2 * (to.y - from.y) / 3}, to};
        };
        // A 4 × 2 rectangle at (10, 20), counter-clockwise in a y-up sense.
        const std::array rectangle{line({10, 20}, {14, 20}), line({14, 20}, {14, 22}), line({14, 22}, {10, 22}), line({10, 22}, {10, 20})};
        const auto measured = mlibs::geom::measure(std::span<const Cubic>(rectangle));
        expect(std::abs(measured.area - 8) < 1e-12 && std::abs(measured.centroid_x - 12) < 1e-12 && std::abs(measured.centroid_y - 21) < 1e-12) << std::format("area {} at ({}, {})", measured.area, measured.centroid_x, measured.centroid_y);
        std::array reversed{line({10, 20}, {10, 22}), line({10, 22}, {14, 22}), line({14, 22}, {14, 20}), line({14, 20}, {10, 20})};
        const auto backwards = mlibs::geom::measure(std::span<const Cubic>(reversed));
        expect(std::abs(backwards.area + 8) < 1e-12 && std::abs(backwards.centroid_x - 12) < 1e-12) << "reversed: the same centre, the opposite sign";
        expect(std::abs(mlibs::geom::normalized_signed_area(std::span<const Cubic>(rectangle)) - 8.0 / 16) < 1e-12) << "area over the larger side squared";
        // A circle of radius 3 about (5, -2) from four cubics: π·9 within the approximation's 1e-3.
        constexpr float k = 3 * 0.5522847498f;
        const std::array circle{Cubic{{8, -2}, {8, -2 + k}, {5 + k, 1}, {5, 1}}, Cubic{{5, 1}, {5 - k, 1}, {2, -2 + k}, {2, -2}},
                                Cubic{{2, -2}, {2, -2 - k}, {5 - k, -5}, {5, -5}}, Cubic{{5, -5}, {5 + k, -5}, {8, -2 - k}, {8, -2}}};
        const auto round = mlibs::geom::measure(std::span<const Cubic>(circle));
        expect(std::abs(round.area - std::numbers::pi * 9) < 1e-2 && std::abs(round.centroid_x - 5) < 1e-6 && std::abs(round.centroid_y + 2) < 1e-6) << std::format("circle area {} at ({}, {})", round.area, round.centroid_x, round.centroid_y);
    };
}};
