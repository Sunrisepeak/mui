// N5: what a clip shows is what is drawn — for a scroll view and the window, drawn ahead: their viewport and as much
// again on each side. A long list's rows past that are not in the display list, yet a reader still finds each where it
// is (offscreen), a pointer reaches only what is shown, and scrolling draws them. What is drawn past a row's box (a
// shadow, a transform) keeps it drawn while that reaches what is seen.
import std;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {

RuntimeOptions options(Size size) {
    RuntimeOptions out;
    out.size = size;
    return out;
}

// The rows under `parent` the display list draws, by id.
std::set<std::string> drawn_rows(const Runtime& runtime, std::string_view parent = "main/list/") {
    std::set<std::string> out;
    for (const auto& command : runtime.display_list().commands())
        if (const auto* begin = std::get_if<xrender::BeginNode>(&command)) {
            const NodeId id = runtime.node_at_index(begin->node);
            if (!runtime.valid(id)) continue;
            const std::string path = runtime.path(id);
            if (path.starts_with(parent) && path.find('/', parent.size()) == std::string::npos) out.insert(path.substr(parent.size()));
        }
    return out;
}

}  // namespace

static UTest cases{"cull", [] {
    "a long list draws what its viewport shows; the rest is where it is, offscreen, and drawn once scrolled to"_test = [] {
        Runtime rt(options({200, 100}));
        auto y = signal(0.0);
        std::vector<Element> rows;
        for (int i = 0; i < 1000; ++i)
            rows.push_back(box({.id = std::format("r{}", i), .height = 30, .background = i % 2 ? "surface" : "accent",
                                .on = {{"press", [](const Value&) {}}}}));
        rt.mount("main", column({}, {scroll({.id = "list", .y = y, .height = 100}, std::move(rows))}));
        expect(rt.frame({seconds(0), {}}).failure.empty());

        // 0–100 shown, drawn ahead to 200: r0 … r7 (r7 from 210, its drawing reaching 16 above its box), no further.
        const auto shown = drawn_rows(rt);
        expect(shown.contains("r0") && shown.contains("r7") && !shown.contains("r8") && shown.size() == 8) << std::format("{} rows drawn", shown.size());
        const NodeId far = rt.find("main/list/r500");
        expect(eq(rt.drawn_bounds(far).y, 15000.0f)) << "a row left out is still where it is";
        expect(rt.semantic_node(far).states.has(State::offscreen));
        expect(!rt.semantic_node(rt.find("main/list/r1")).states.has(State::offscreen));
        expect(rt.hit_test({50, 50}) == rt.find("main/list/r1")) << "a pointer reaches what is shown";

        // Scrolled to it: drawn, shown, hit; the first rows left out in their turn.
        y.set(15000.0);
        expect(rt.frame({seconds(1), {}}).failure.empty());
        const auto later = drawn_rows(rt);
        // 15000–15100 shown, 14900–15200 drawn: r496 (to 14910, reaching 14926) … r507 (from 15210, reaching 15194).
        expect(later.contains("r496") && later.contains("r500") && later.contains("r507") && !later.contains("r495") && !later.contains("r508") &&
               !later.contains("r0") && later.size() == 12)
            << std::format("{} rows drawn", later.size());
        expect(!rt.semantic_node(far).states.has(State::offscreen));
        expect(rt.hit_test({50, 10}) == far);
        expect(rt.semantic_node(rt.find("main/list/r0")).states.has(State::offscreen));
        // Scrolled into view by a reader, as it found it.
        expect(eq(rt.act(rt.find("main/list/r900"), "scroll_into_view", {}).exit_code().value_or(-1), 0));
        expect(rt.frame({seconds(3), {}}).failure.empty());
        expect(drawn_rows(rt).contains("r900"));
    };

    "what is drawn past a row's box keeps it drawn while that reaches the viewport: a shadow, a transform"_test = [] {
        Runtime rt(options({200, 100}));
        std::vector<Element> rows;
        for (int i = 0; i < 12; ++i) {
            // r4 (120–150) casts md (4 down, 10 blur: about 24 past its box); r5 (150–180) the same, too far;
            // r10 (300–330) moved up 280 by its transform, onto 20–50.
            Element row = box({.id = std::format("r{}", i), .height = 30, .background = "surface",
                               .shadow = i == 4 || i == 5 ? std::string("md") : std::string()});
            if (i == 10) row.prop("translate_y", Source(Value(-280.0)));
            rows.push_back(std::move(row));
        }
        // A clip that does not scroll: exactly what it shows.
        Element list = column({.id = "list", .height = 100}, std::move(rows));
        list.prop("clip", Source(Value(true)));
        rt.mount("main", column({}, {std::move(list)}));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        const auto shown = drawn_rows(rt);
        expect(shown.contains("r4")) << "its shadow reaches the viewport";
        expect(!shown.contains("r5") && !shown.contains("r6")) << "nothing of them does";
        expect(shown.contains("r10")) << "moved into the viewport by its transform";
        expect(!shown.contains("r11"));
    };

    "a clip that does not scroll leaves out what lies outside it; without one, the window is the clip"_test = [] {
        Runtime rt(options({200, 300}));
        std::vector<Element> clipped, open;
        for (int i = 0; i < 20; ++i) clipped.push_back(box({.id = std::format("r{}", i), .height = 30, .background = "surface"}));
        for (int i = 0; i < 20; ++i) open.push_back(box({.id = std::format("o{}", i), .height = 30, .background = "surface"}));
        Element list = column({.id = "list", .height = 60}, std::move(clipped));
        list.prop("clip", Source(Value(true)));
        rt.mount("main", column({}, {std::move(list), column({.id = "open", .height = 60}, std::move(open))}));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        expect(drawn_rows(rt) == std::set<std::string>{"r0", "r1", "r2"}) << "60 shown, r2 within the 16 its drawing may reach";
        // The open column's rows overflow it (60 + 30 k) and are drawn while the window (300, drawn ahead to 600) can
        // show them: o0 … o18 (from 600, reaching 584).
        const auto open_drawn = drawn_rows(rt, "main/open/");
        expect(eq(open_drawn.size(), std::size_t{19}) && open_drawn.contains("o18") && !open_drawn.contains("o19")) << std::format("{} drawn", open_drawn.size());
        expect(eq(rt.drawn_bounds(rt.find("main/open/o19")).y, 630.0f)) << "past the window, still where it is";
    };

    "rows inside a column inside a scroll view: the viewport reaches them through the column"_test = [] {
        Runtime rt(options({200, 100}));
        auto y = signal(0.0);
        std::vector<Element> rows;
        for (int i = 0; i < 500; ++i) rows.push_back(box({.id = std::format("r{}", i), .height = 30, .background = "surface"}));
        rt.mount("main", column({}, {scroll({.id = "view", .y = y, .height = 100}, {column({.id = "list"}, std::move(rows))})}));
        expect(rt.frame({seconds(0), {}}).failure.empty());
        const auto first = drawn_rows(rt, "main/view/list/");
        expect(first.contains("r7") && !first.contains("r8") && first.size() == 8) << std::format("{} rows drawn", first.size());
        y.set(3000.0);
        expect(rt.frame({seconds(1), {}}).failure.empty());
        const auto later = drawn_rows(rt, "main/view/list/");
        expect(later.contains("r96") && later.contains("r100") && later.contains("r107") && !later.contains("r95") && later.size() == 12)
            << std::format("{} rows drawn", later.size());
    };

    "a row with motion of its own, scrolled out and back: it was there all along — no entrance, no update"_test = [] {
        Runtime rt(options({200, 100}));
        auto y = signal(0.0);
        std::vector<Element> rows;
        for (int i = 0; i < 100; ++i) {
            Element row = box({.id = std::format("r{}", i), .height = 30, .background = "surface"});
            row.prop("motion", Source(Value(Value::Map{{"enter", Value("morph motion.normal")}, {"update", Value("morph motion.normal")}})));
            rows.push_back(std::move(row));
        }
        rt.mount("main", column({}, {scroll({.id = "view", .y = y, .height = 100}, {column({.id = "list"}, std::move(rows))})}));
        rt.frame({seconds(0), {}});
        rt.frame({seconds(0.016), {}});
        const auto rows_moving = [&] {
            return std::ranges::count_if(rt.motions(), [](const MotionState& m) { return m.path.starts_with("main/view/list/r"); });
        };
        expect(eq(rows_moving(), 0)) << "there from the start";
        expect(drawn_rows(rt, "main/view/list/").contains("r0"));
        y.set(900.0);
        rt.frame({seconds(1), {}});
        expect(eq(rows_moving(), 0)) << "rows scrolled out of view do not leave";
        rt.frame({seconds(1.016), {}});
        expect(!drawn_rows(rt, "main/view/list/").contains("r0") && drawn_rows(rt, "main/view/list/").contains("r30")) << "r0 left out, r30 drawn";
        expect(eq(rows_moving(), 0)) << "rows scrolled into view do not come in";
        y.set(0.0);
        rt.frame({seconds(2), {}});
        expect(drawn_rows(rt, "main/view/list/").contains("r0"));
        expect(eq(rows_moving(), 0)) << "nor do they scrolled back";
    };
}};
