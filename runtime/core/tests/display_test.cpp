import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.display"};

    s.test("affine transforms", [] {
        auto m = omui::Affine::translate(10, 5) * omui::Affine::scale(2, 3);
        auto p = m.apply({1, 1});
        check(p == omui::Point{12, 8});
        auto inv = m.inverse();
        check(inv.has_value());
        auto back = inv->apply(p);
        check_near(back.x, 1, 1e-6);
        check_near(back.y, 1, 1e-6);
        check(!omui::Affine::scale(0, 1).inverse().has_value());
        auto r = omui::Affine::rotate(omui::anim::dm::half_pi).apply({1, 0});
        check_near(r.x, 0, 1e-6);
        check_near(r.y, 1, 1e-6);
        check(omui::Affine{}.identity());
    });

    s.test("paths", [] {
        omui::Path p;
        p.add_rounded_rect({0, 0, 100, 40}, omui::CornerRadii::all(8));
        check(p.bounds() == omui::Rect{0, 0, 100, 40});
        omui::Path q;
        q.add_rounded_rect({0, 0, 10, 10}, omui::CornerRadii::all(50));  // radii clamp to the box
        check(q.bounds() == omui::Rect{0, 0, 10, 10});
        omui::Path arc;
        arc.move_to({0, 0}).arc_to({50, 50}, 0, false, true, {100, 0});
        auto end = arc.points().back();
        check_near(end.x, 100, 1e-3);
        check_near(end.y, 0, 1e-3);
        check(arc.bounds().y < -45, "a half circle bulges by its radius");
        auto moved = p.transformed(omui::Affine::translate(5, 5));
        check(moved.bounds() == omui::Rect{5, 5, 100, 40});
    });

    s.test("lists hash by content and stay balanced", [] {
        auto build = [](omui::DisplayList& dl, float x) {
            dl.begin_node(1);
            dl.push_clip({0, 0, 100, 100}, omui::CornerRadii::all(4));
            dl.fill_round_rect({x, 0, 50, 20}, omui::CornerRadii::all(6), omui::Paint::solid(omui::Color::hex(0x336699)));
            omui::Path p;
            p.move_to({0, 0}).line_to({10, 10});
            dl.stroke_path(p, {.width = 2}, omui::Paint::solid(omui::Color{}));
            std::array<omui::Glyph, 2> glyphs{{{5, {0, 0}}, {6, {7, 0}}}};
            dl.draw_glyphs({1}, 14, {2, 12}, glyphs, omui::Paint::solid(omui::Color{}));
            dl.pop_clip();
            dl.end_node();
        };
        omui::DisplayList a, b, c;
        build(a, 1);
        build(b, 1);
        build(c, 2);
        check(a.balanced());
        check_eq(a.hash(), b.hash());
        check(a.hash() != c.hash());
        omui::DisplayList d;
        d.append(a);
        check_eq(d.hash(), a.hash());
        a.clear();
        check(a.empty());
        check_eq(d.hash(), b.hash(), std::source_location::current());
        omui::DisplayList bad;
        bad.pop_layer();
        check(!bad.balanced());
    });

    s.test("a frame of the same shape allocates nothing new", [] {
        omui::Arena arena(4096);
        for (int frame = 0; frame < 10; ++frame) {
            for (int i = 0; i < 100; ++i) (void)arena.make<omui::Rect>(1.0f, 2.0f, 3.0f, 4.0f);
            (void)arena.copy(std::string_view("hello"));
            arena.reset();
        }
        auto after_first = arena.heap_allocations();
        for (int frame = 0; frame < 100; ++frame) {
            for (int i = 0; i < 100; ++i) (void)arena.make<omui::Rect>();
            arena.reset();
        }
        check_eq(arena.heap_allocations(), after_first);
        auto big = arena.array<float>(100000);
        check(big.size() == 100000);
    });

    return s.run(argc, argv);
}
