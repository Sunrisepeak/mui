import std;
import openmui.render.vector;
import openmui.geom;
import openmui.memory;
import hanim;
import openmui.testing;

using namespace omui::testing;
namespace render = omui::render;
namespace geom = omui::geom;

int main(int argc, char** argv) {
    Suite s{"render.display"};

    s.test("affine transforms", [] {
        auto m = geom::Affine::translate(10, 5) * geom::Affine::scale(2, 3);
        auto p = m.apply({1, 1});
        check(p == geom::Point{12, 8});
        auto inv = m.inverse();
        check(inv.has_value());
        auto back = inv->apply(p);
        check_near(back.x, 1, 1e-6);
        check_near(back.y, 1, 1e-6);
        check(!geom::Affine::scale(0, 1).inverse().has_value());
        auto r = render::rotate(hanim::dm::half_pi).apply({1, 0});
        check_near(r.x, 0, 1e-6);
        check_near(r.y, 1, 1e-6);
        check(geom::Affine{}.identity());
    });

    s.test("paths", [] {
        render::Path p;
        p.add_rounded_rect({0, 0, 100, 40}, geom::CornerRadii::all(8));
        check(p.bounds() == geom::Rect{0, 0, 100, 40});
        render::Path q;
        q.add_rounded_rect({0, 0, 10, 10}, geom::CornerRadii::all(50));  // radii clamp to the box
        check(q.bounds() == geom::Rect{0, 0, 10, 10});
        render::Path arc;
        arc.move_to({0, 0}).arc_to({50, 50}, 0, false, true, {100, 0});
        auto end = arc.points().back();
        check_near(end.x, 100, 1e-3);
        check_near(end.y, 0, 1e-3);
        check(arc.bounds().y < -45, "a half circle bulges by its radius");
        auto moved = p.transformed(geom::Affine::translate(5, 5));
        check(moved.bounds() == geom::Rect{5, 5, 100, 40});
    });

    s.test("a range of a list recoloured: its solid paints and shadows, not the commands around it", [] {
        render::DisplayList dl;
        dl.fill_rect({0, 0, 10, 10}, render::Paint::solid(geom::Color::hex(0xff0000)));
        dl.fill_rect({0, 0, 10, 10}, render::Paint::solid(geom::Color::hex(0x00ff00)));
        dl.box_shadow({0, 0, 10, 10}, {}, {2, 2}, 4, 0, geom::Color::hex(0x0000ff), false);
        dl.fill_rect({0, 0, 10, 10}, render::Paint::solid(geom::Color::hex(0xffff00)));
        const auto grey = [](geom::Color c) {
            const float y = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            return geom::Color{y, y, y, c.a};
        };
        dl.recolor(1, 3, grey);
        auto colour = [&](std::size_t i) {
            return std::visit([](const auto& c) -> geom::Color {
                if constexpr (requires { c.paint; }) return c.paint.color;
                else if constexpr (requires { c.color; }) return c.color;
                else return {};
            }, dl.commands()[i]);
        };
        check(colour(0) == geom::Color::hex(0xff0000) && colour(3) == geom::Color::hex(0xffff00), "outside the range: as drawn");
        check(colour(1).r == colour(1).g && colour(1).g == colour(1).b && colour(2).r == colour(2).b, "inside: grey");
    });

    s.test("lists hash by content and stay balanced", [] {
        auto build = [](render::DisplayList& dl, float x) {
            dl.begin_node(1);
            dl.push_clip({0, 0, 100, 100}, geom::CornerRadii::all(4));
            dl.fill_round_rect({x, 0, 50, 20}, geom::CornerRadii::all(6), render::Paint::solid(geom::Color::hex(0x336699)));
            render::Path p;
            p.move_to({0, 0}).line_to({10, 10});
            dl.stroke_path(p, {.width = 2}, render::Paint::solid(geom::Color{}));
            std::array<render::Glyph, 2> glyphs{{{5, {0, 0}}, {6, {7, 0}}}};
            dl.draw_glyphs({1}, 14, {2, 12}, glyphs, render::Paint::solid(geom::Color{}));
            dl.pop_clip();
            dl.end_node();
        };
        render::DisplayList a, b, c;
        build(a, 1);
        build(b, 1);
        build(c, 2);
        check(a.balanced());
        check_eq(a.hash(), b.hash());
        check(a.hash() != c.hash());
        render::DisplayList d;
        d.append(a);
        check_eq(d.hash(), a.hash());
        a.clear();
        check(a.empty());
        check_eq(d.hash(), b.hash(), std::source_location::current());
        render::DisplayList bad;
        bad.pop_layer();
        check(!bad.balanced());
    });

    return s.run(argc, argv);
}
