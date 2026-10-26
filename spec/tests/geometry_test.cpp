import std;
import openmui.spec;
import openmui.testing;

using namespace omui::testing;
namespace spec = omui::spec;

int main(int argc, char** argv) {
    Suite s{"spec.geometry"};

    s.test("rounding is half up, negatives included", [] {
        check_eq(spec::snap(0.5f, 1), 1);
        check_eq(spec::snap(0.49f, 1), 0);
        check_eq(spec::snap(-0.5f, 1), 0);
        check_eq(spec::snap(-0.51f, 1), -1);
        check_eq(spec::snap(10.25f, 2), 21);
        check_eq(spec::snap(1.0f / 3.0f, 3), 1);
    });

    s.test("rectangles that share an edge share it at every scale", [] {
        for (float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f}) {
            float x = 0;
            spec::PixelRect prev{};
            for (int i = 0; i < 200; ++i) {
                float w = 7.3f + static_cast<float>(i % 7) * 0.37f;
                auto r = spec::snap(spec::Rect{x, 0, w, 10}, scale);
                if (i > 0) check_eq(r.x, prev.x + prev.width);
                prev = r;
                x += w;
            }
        }
    });

    s.test("rectangle operations", [] {
        spec::Rect a{0, 0, 10, 10}, b{5, 5, 10, 10};
        check(a.intersects(b));
        check(a.intersection(b) == spec::Rect{5, 5, 5, 5});
        check(a.united(b) == spec::Rect{0, 0, 15, 15});
        check(a.contains({0, 0}) && !a.contains({10, 5}));
        check(!a.intersects(spec::Rect{10, 0, 5, 5}), "touching is not intersecting");
        check(a.inset(spec::Insets::all(2)) == spec::Rect{2, 2, 6, 6});
        check(spec::Rect{}.united(a) == a);
    });

    s.test("colours", [] {
        auto c = spec::Color::parse("#ff800080");
        check(c.has_value());
        auto p = c->premultiplied8();
        check_eq(int(p[0]), 128);
        check_eq(int(p[1]), 64);
        check_eq(int(p[3]), 128);
        check(spec::Color::parse("#fff") == spec::Color::rgba8(255, 255, 255));
        check(!spec::Color::parse("#ffff0").has_value());
        check(spec::Color::hex(0x336699) == spec::Color::rgba8(0x33, 0x66, 0x99));
    });

    return s.run(argc, argv);
}
