// Layout against what CSS (border-box) computes for the same boxes, plus
// incrementality and a pinned determinism hash.
import std;
import openmui.geom;
import openmui.layout;
import openmui.testing;

using namespace omui::testing;
namespace layout = omui::layout;
using layout::pt;
using layout::pct;
using omui::geom::Rect;

namespace {

void check_frame(const layout::Engine& e, layout::NodeId n, Rect expected,
                 std::source_location where = std::source_location::current()) {
    Rect f = e.frame(n);
    bool ok = std::abs(f.x - expected.x) < 1e-3 && std::abs(f.y - expected.y) < 1e-3 &&
              std::abs(f.width - expected.width) < 1e-3 && std::abs(f.height - expected.height) < 1e-3;
    check(ok, std::format("frame {{{}, {}, {}, {}}}, expected {{{}, {}, {}, {}}}", f.x, f.y, f.width, f.height, expected.x,
                          expected.y, expected.width, expected.height),
          where);
}

layout::Style box(float w, float h) {
    layout::Style s;
    s.width = pt(w);
    s.height = pt(h);
    return s;
}

// A paragraph: words of 10 points each, 10 points apart, lines 20 points high.
struct Words {
    int count;
};

omui::geom::Size measure_words(void* ctx, layout::NodeId, std::optional<float> width, std::optional<float>,
                               layout::Available aw, layout::Available) {
    int n = static_cast<Words*>(ctx)->count;
    float max_w = 0;
    if (width) max_w = *width;
    else if (aw.kind == layout::Available::Kind::min_content) max_w = 0;
    else if (aw.kind == layout::Available::Kind::definite) max_w = aw.value;
    else max_w = std::numeric_limits<float>::infinity();
    int per_line = max_w == std::numeric_limits<float>::infinity() ? n : std::max(1, static_cast<int>((max_w + 10) / 20));
    per_line = std::min(per_line, n);
    int lines = (n + per_line - 1) / per_line;
    return {static_cast<float>(per_line * 20 - 10), static_cast<float>(lines * 20)};
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"layout"};

    s.test("row: padding, border, gap, fixed children", [] {
        layout::Engine e;
        layout::Style rs;
        rs.padding = omui::geom::Insets::all(10);
        rs.border = omui::geom::Insets::all(1);
        rs.column_gap = 5;
        auto root = e.create(rs);
        auto a = e.create(box(50, 20));
        auto b = e.create(box(30, 40));
        e.append(root, a);
        e.append(root, b);
        e.compute(root, {400, 300});
        check_frame(e, root, {0, 0, 400, 300});
        check_frame(e, a, {11, 11, 50, 20});
        check_frame(e, b, {66, 11, 30, 40});
    });

    s.test("grow shares free space by factor, within max", [] {
        layout::Engine e;
        auto root = e.create();
        layout::Style g1 = box(0, 10), g2 = box(0, 10), g3 = box(0, 10);
        g1.width = layout::Length::automatic();
        g2.width = layout::Length::automatic();
        g3.width = layout::Length::automatic();
        g1.grow = 1;
        g2.grow = 2;
        g3.grow = 1;
        g3.max_width = pt(50);
        auto a = e.create(g1), b = e.create(g2), c = e.create(g3);
        for (auto n : {a, b, c}) e.append(root, n);
        e.compute(root, {400, 100});
        // c is frozen at its max of 50; a and b share 350 as 1:2.
        check_frame(e, c, {350, 0, 50, 10});
        check_frame(e, a, {0, 0, 350.0f / 3, 10});
        check_frame(e, b, {350.0f / 3, 0, 700.0f / 3, 10});
    });

    s.test("shrink by base size, stopping at the automatic minimum", [] {
        layout::Engine e;
        layout::Style rs;
        rs.align_items = layout::Align::start;
        auto root = e.create(rs);
        Words five{5}, two{2};
        auto a = e.create(), b = e.create();
        e.set_measure(a, measure_words, &five);
        e.set_measure(b, measure_words, &two);
        e.append(root, a);
        e.append(root, b);
        // max-content: a 90, b 30 = 120 in 60: shrink by 60 in proportion 90:30.
        e.compute(root, {60, 100});
        // a would become 45 but its min-content (one word, 10) is lower; b becomes 15.
        check_frame(e, a, {0, 0, 45, 60});   // 45 wide: 2 words per line, 3 lines
        check_frame(e, b, {45, 0, 15, 40});  // 15 wide: 1 word per line, 2 lines
    });

    s.test("column: stretch, center, end, space-between", [] {
        layout::Engine e;
        layout::Style cs;
        cs.direction = layout::Direction::column;
        cs.justify = layout::Justify::space_between;
        auto root = e.create(cs);
        layout::Style a = box(0, 20);
        a.width = layout::Length::automatic();
        layout::Style b = box(40, 20);
        b.align_self = layout::Align::center;
        layout::Style c = box(40, 20);
        c.align_self = layout::Align::end;
        auto na = e.create(a), nb = e.create(b), nc = e.create(c);
        for (auto n : {na, nb, nc}) e.append(root, n);
        e.compute(root, {200, 100});
        check_frame(e, na, {0, 0, 200, 20});
        check_frame(e, nb, {80, 40, 40, 20});
        check_frame(e, nc, {160, 80, 40, 20});
    });

    s.test("justify: around and evenly", [] {
        for (auto [j, first] : {std::pair{layout::Justify::space_around, 25.0f}, std::pair{layout::Justify::space_evenly, 100.0f / 3}}) {
            layout::Engine e;
            layout::Style rs;
            rs.justify = j;
            rs.align_items = layout::Align::start;
            auto root = e.create(rs);
            auto a = e.create(box(50, 10)), b = e.create(box(50, 10));
            e.append(root, a);
            e.append(root, b);
            e.compute(root, {200, 50});
            check_near(e.frame(a).x, first, 1e-3);
        }
    });

    s.test("wrap into lines; wrap-reverse", [] {
        layout::Engine e;
        layout::Style rs;
        rs.wrap = layout::Wrap::wrap;
        rs.align_items = layout::Align::start;
        rs.row_gap = 4;
        rs.column_gap = 4;
        auto root = e.create(rs);
        std::vector<layout::NodeId> kids;
        for (int i = 0; i < 5; ++i) {
            kids.push_back(e.create(box(40, 10 + static_cast<float>(i))));
            e.append(root, kids.back());
        }
        e.compute(root, {130, 200});
        // 3 per line (40+4+40+4+40 = 128 ≤ 130).
        check_frame(e, kids[2], {88, 0, 40, 12});
        check_frame(e, kids[3], {0, 16, 40, 13});
        layout::Style rev = rs;
        rev.wrap = layout::Wrap::wrap_reverse;
        rev.height = pt(100);
        e.set_style(root, rev);
        e.compute(root, {130, 200});
        check_near(e.frame(kids[0]).y, 100 - 12, 1e-3);
    });

    s.test("percentages, min/max, aspect ratio", [] {
        layout::Engine e;
        layout::Style rs;
        rs.padding = omui::geom::Insets::all(10);
        rs.align_items = layout::Align::start;
        auto root = e.create(rs);
        layout::Style a;
        a.width = pct(50);
        a.aspect_ratio = 2;
        layout::Style b;
        b.width = pct(80);
        b.max_width = pt(100);
        b.height = pt(10);
        auto na = e.create(a), nb = e.create(b);
        e.append(root, na);
        e.append(root, nb);
        e.compute(root, {220, 200});
        check_frame(e, na, {10, 10, 100, 50});
        check_frame(e, nb, {110, 10, 100, 10});
    });

    s.test("absolute positioning in the padding box", [] {
        layout::Engine e;
        layout::Style rs;
        rs.padding = omui::geom::Insets::all(10);
        rs.border = omui::geom::Insets::all(2);
        auto root = e.create(rs);
        layout::Style a = box(30, 30);
        a.position = layout::Position::absolute;
        a.right = pt(5);
        a.bottom = pt(5);
        layout::Style b;
        b.position = layout::Position::absolute;
        b.left = pt(0);
        b.right = pt(0);
        b.top = pt(0);
        b.height = pt(4);
        auto na = e.create(a), nb = e.create(b);
        e.append(root, na);
        e.append(root, nb);
        e.compute(root, {200, 100});
        check_frame(e, na, {200 - 2 - 5 - 30, 100 - 2 - 5 - 30, 30, 30});
        check_frame(e, nb, {2, 2, 196, 4});
    });

    s.test("grid: fixed, fr, auto, spans, auto-placement", [] {
        layout::Engine e;
        layout::Style gs;
        gs.display = layout::Display::grid;
        gs.columns = {layout::Track::points(100), layout::Track::fr(1), layout::Track::fr(2)};
        gs.column_gap = 10;
        gs.row_gap = 5;
        gs.auto_rows = layout::Track::points(30);
        auto root = e.create(gs);
        std::vector<layout::NodeId> cells;
        for (int i = 0; i < 4; ++i) {
            layout::Style c;
            if (i == 1) c.column_start = layout::GridLine::spanning(2);
            cells.push_back(e.create(c));
            e.append(root, cells.back());
        }
        e.compute(root, {400, 300});
        // fr: (400 − 100 − 20) / 3 = 93.33 per fr.
        float fr = 280.0f / 3;
        check_frame(e, cells[0], {0, 0, 100, 30});
        check_frame(e, cells[1], {110, 0, fr + 10 + 2 * fr, 30});
        check_frame(e, cells[2], {0, 35, 100, 30});
        check_frame(e, cells[3], {110, 35, fr, 30});
    });

    s.test("grid: explicit and negative lines; auto tracks fit content", [] {
        layout::Engine e;
        layout::Style gs;
        gs.display = layout::Display::grid;
        gs.columns = {layout::Track::automatic(), layout::Track::points(50)};
        gs.rows = {layout::Track::automatic()};
        gs.justify = layout::Justify::start;
        gs.width = pt(200);
        auto root = e.create(gs);
        layout::Style last;
        last.column_start = layout::GridLine::at(-1);
        last.width = pt(20);
        last.height = pt(20);
        layout::Style first = box(70, 15);
        auto a = e.create(last), b = e.create(first);
        e.append(root, a);
        e.append(root, b);
        e.compute(root, {400, 300});
        // b auto-places in column 1 (the first free cell); the auto column
        // takes 70 and stretches over the free space: 200 − 50 = 150.
        check_frame(e, b, {0, 0, 70, 15});
        check_near(e.frame(a).x, 150, 1e-3);
    });

    s.test("stack overlays its children", [] {
        layout::Engine e;
        layout::Style ss;
        ss.display = layout::Display::stack;
        ss.align_items = layout::Align::center;
        auto root = e.create(ss);
        auto big = e.create(box(100, 60)), small = e.create(box(20, 10));
        e.append(root, big);
        e.append(root, small);
        layout::Style wrap;
        wrap.align_items = layout::Align::start;
        auto outer = e.create(wrap);
        e.append(outer, root);
        e.compute(outer, {500, 500});
        check_frame(e, root, {0, 0, 100, 60});
        check_frame(e, small, {40, 25, 20, 10});
    });

    s.test("scroll containers keep their content's size", [] {
        layout::Engine e;
        layout::Style ss;
        ss.direction = layout::Direction::column;
        ss.overflow_y = layout::Overflow::scroll;
        ss.height = pt(100);
        auto root = e.create(ss);
        for (int i = 0; i < 10; ++i) e.append(root, e.create(box(50, 30)));
        e.compute(root, {200, 500});
        check_near(e.content_size(root).height, 300, 1e-3);
        check_frame(e, e.children(root)[9], {0, 270, 50, 30});
    });

    s.test("relayout is incremental", [] {
        layout::Engine e;
        layout::Style cs;
        cs.direction = layout::Direction::column;
        auto root = e.create(cs);
        std::vector<Words> words(200, Words{12});
        std::vector<layout::NodeId> paras;
        for (auto& w : words) {
            paras.push_back(e.create());
            e.set_measure(paras.back(), measure_words, &w);
            e.append(root, paras.back());
        }
        e.compute(root, {300, 1000});
        e.reset_stats();
        e.compute(root, {300, 1000});
        check_eq(e.stats().layouts, std::size_t{0}, std::source_location::current());
        words[100].count = 30;
        e.mark_dirty(paras[100]);
        e.reset_stats();
        e.compute(root, {300, 1000});
        // The changed paragraph (measured and laid out under its few distinct
        // constraints) and the root; the other 199 paragraphs come from cache.
        check(e.stats().layouts <= 6, std::format("{} nodes laid out after one change", e.stats().layouts));
        check(e.stats().measures <= 4, std::format("{} measures after one change", e.stats().measures));
        check_near(e.frame(paras[100]).height, 40, 1e-3);
        check_near(e.frame(paras[101]).y, e.frame(paras[100]).y + 40, 1e-3);
    });

    s.test("display none takes no room", [] {
        layout::Engine e;
        auto root = e.create();
        layout::Style hidden = box(50, 50);
        hidden.display = layout::Display::none;
        auto a = e.create(hidden), b = e.create(box(10, 10));
        e.append(root, a);
        e.append(root, b);
        e.compute(root, {100, 100});
        check_frame(e, b, {0, 0, 10, 10});
        check(e.frame(a).empty());
    });

    s.test("pinned frames of a large tree", [] {
        layout::Engine e;
        std::uint64_t seed = 42;
        auto next = [&] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<float>((seed >> 33) % 1000) / 10.0f;
        };
        layout::Style rs;
        rs.wrap = layout::Wrap::wrap;
        rs.column_gap = 3.5f;
        rs.row_gap = 2.25f;
        rs.padding = omui::geom::Insets::all(7.3f);
        auto root = e.create(rs);
        std::vector<layout::NodeId> all{root};
        for (int i = 0; i < 300; ++i) {
            layout::Style c;
            c.width = pt(next());
            c.height = pt(next());
            c.grow = next() / 50;
            c.shrink = next() / 50;
            c.margin = omui::geom::Insets::all(next() / 30);
            if (i % 7 == 0) {
                c.direction = layout::Direction::column;
                c.width = layout::Length::automatic();
                c.basis = pct(next() / 4);
            }
            auto n = e.create(c);
            e.append(all[static_cast<std::size_t>(i % 11 == 0 ? 0 : (i / 3))], n);
            all.push_back(n);
        }
        e.compute(root, {1280.5f, 720.25f});
        Hash h;
        for (auto n : all) {
            auto f = e.frame(n);
            h.f32(f.x);
            h.f32(f.y);
            h.f32(f.width);
            h.f32(f.height);
        }
        std::println("    layout frames {}", h.hex());
        check_eq(h.hex(), std::string("6030294bb8dd27d9"));
    });

    return s.run(argc, argv);
}
