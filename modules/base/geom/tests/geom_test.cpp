import std;
import openmui.geom;
import openmui.testing;

using namespace omui::testing;
namespace geom = omui::geom;

static_assert(geom::snap(0.5f, 1) == 1 && geom::snap(-0.51f, 1) == -1);
static_assert(geom::Rect{0, 0, 10, 10}.intersects(geom::Rect{5, 5, 1, 1}));

int main(int argc, char** argv) {
    Suite s{"geom"};

    s.test("rounding is half up, negatives included", [] {
        check_eq(geom::snap(0.49f, 1), 0);
        check_eq(geom::snap(-0.5f, 1), 0);
        check_eq(geom::snap(10.25f, 2), 21);
        check_eq(geom::snap(1.0f / 3.0f, 3), 1);
    });

    s.test("rectangles that share an edge share it at every scale", [] {
        for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
            float x = 0;
            geom::PixelRect prev{};
            for (int i = 0; i < 200; ++i) {
                float w = 7.3f + static_cast<float>(i % 7) * 0.37f;
                auto r = geom::snap(geom::Rect{x, 0, w, 10}, scale);
                if (i > 0) check_eq(r.x, prev.x + prev.width);
                prev = r;
                x += w;
            }
        }
    });

    s.test("rectangles", [] {
        geom::Rect a{0, 0, 10, 10}, b{5, 5, 10, 10};
        check(a.intersection(b) == geom::Rect{5, 5, 5, 5});
        check(a.united(b) == geom::Rect{0, 0, 15, 15});
        check(a.contains(geom::Point{0, 0}) && !a.contains(geom::Point{10, 5}));
        check(a.contains(geom::Rect{1, 1, 9, 9}));
        check(!a.intersects(geom::Rect{10, 0, 5, 5}), "touching is not intersecting");
        check(a.inset(geom::Insets::all(2)) == geom::Rect{2, 2, 6, 6});
        check(a.inset(geom::Insets::all(2)).outset(geom::Insets::all(2)) == a);
    });

    s.test("radii fit their box", [] {
        auto r = geom::CornerRadii::all(50).fitted({10, 20});
        check(r == geom::CornerRadii::all(5));
        check(geom::CornerRadii::all(3).fitted({100, 100}) == geom::CornerRadii::all(3));
    });

    s.test("colours", [] {
        auto c = geom::Color::parse("#ff800080");
        auto p = c->premultiplied8();
        check_eq(int(p[0]), 128);
        check_eq(int(p[1]), 64);
        check_eq(int(p[3]), 128);
        check(geom::Color::parse("#fff") == geom::Color::white());
        check(!geom::Color::parse("#ffff0").has_value());
    });

    s.test("affine transforms", [] {
        auto m = geom::Affine::translate(10, 5) * geom::Affine::scale(2, 3);
        check(m.apply({1, 1}) == geom::Point{12, 8});
        auto back = m.inverse()->apply({12, 8});
        check_near(back.x, 1, 1e-6);
        check_near(back.y, 1, 1e-6);
        check(!geom::Affine::scale(0, 1).inverse().has_value());
        check_near(geom::Affine::scale(3, 2).max_scale(), 3, 1e-5);
        check(geom::Affine::translate(1, 1).map({0, 0, 2, 2}) == geom::Rect{1, 1, 2, 2});
    });

    return s.run(argc, argv);
}
