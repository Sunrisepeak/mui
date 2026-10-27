// canvas-kit (canvas-markdown §1.1–1.3): each behaviour does what a person
// does with a mouse and what an agent does through the node's actions —
// the same thing — and tells the canvas's listeners when it is done;
// declarative behaviours load from TOML, run, and refuse a misspelt file
// with the line of each problem.
import std;

import openmui.ui.canvas_kit;
import openmui.host.headless;
import openmui.render;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

// The canvas fills an 800 × 600 window with the camera at the origin, zoom
// 1: world (x, y) is on screen at (400 + x, 300 + y).
constexpr Point screen(double x, double y) { return {static_cast<float>(400 + x), static_cast<float>(300 + y)}; }

PointerEvent press(Point at, Modifiers mods = {}, std::uint8_t clicks = 1) {
    return {.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1, .mods = mods, .clicks = clicks};
}
PointerEvent drag_to(Point at, Modifiers mods = {}) { return {.kind = PointerKind::move, .position = at, .buttons = 1, .mods = mods}; }
PointerEvent release(Point at, Modifiers mods = {}) { return {.kind = PointerKind::up, .position = at, .button = MouseButton::left, .mods = mods}; }

void drag(Headless& h, Point from, Point to, Modifiers mods = {}) {
    h.step({press(from, mods)});
    // In steps, as a hand moves.
    for (int i = 1; i <= 4; ++i)
        h.step({drag_to({from.x + (to.x - from.x) * static_cast<float>(i) / 4, from.y + (to.y - from.y) * static_cast<float>(i) / 4}, mods)});
    h.step({release(to, mods)});
}

void click(Headless& h, Point at, Modifiers mods = {}) {
    h.step({press(at, mods)});
    h.step({release(at, mods)});
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "test", {}};
    call.answer = std::move(answer);
    auto e = h.tree().act(id, action, call);
    h.step();
    return e.exit_code().value_or(-1);
}

std::vector<std::string> action_names(Headless& h, std::string_view id) {
    std::vector<std::string> out;
    if (auto n = h.tree().node(id))
        for (const auto& a : n->actions) out.push_back(a.name);
    return out;
}

bool has(const std::vector<std::string>& v, std::string_view x) { return std::ranges::find(v, x) != v.end(); }

world::Scene& board(std::string_view name) {
    auto& s = world::scene(name);
    s.clear();
    s.add({.kind = "box", .x = 0, .y = 0, .width = 100, .height = 80, .key = "a", .fill = Color::hex(0x16a34a)});
    s.add({.kind = "box", .x = 200, .y = 0, .width = 100, .height = 80, .key = "b", .fill = Color::hex(0x2563eb)});
    s.add({.kind = "box", .x = -250, .y = 150, .width = 60, .height = 60, .key = "c", .fill = Color::hex(0xdc2626)});
    return s;
}

struct Fixture {
    std::vector<std::string>                 selection;
    std::vector<std::pair<std::string, int>> events;
    Headless                                 h{{.size = {800, 600}}};

    Fixture(std::string_view scene, std::vector<std::string> behaviors) {
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board",
                                        .behaviors = std::move(behaviors),
                                        .scene = std::string(scene),
                                        .on_connected = [this](std::string, std::string, std::string) { events.emplace_back("connected", 1); },
                                        .on_deleted = [this](std::vector<std::string> keys) { events.emplace_back("deleted", static_cast<int>(keys.size())); },
                                        .on_disconnected = [this](std::string, std::string, std::string) { events.emplace_back("disconnected", 1); },
                                        .on_moved = [this](double, double, std::vector<std::string> keys) { events.emplace_back("moved", static_cast<int>(keys.size())); },
                                        .on_resized = [this](double, std::string, double, double, double) { events.emplace_back("resized", 1); },
                                        .on_selection_changed = [this](std::vector<std::string> keys) { selection = std::move(keys); },
                                        .width = 800,
                                        .height = 600})}));
        h.settle();
    }
    bool saw(std::string_view what) const { return std::ranges::find(events, what, &std::pair<std::string, int>::first) != events.end(); }
};


constexpr std::string_view group_drag = R"toml(
# group-drag.behavior.omui.toml
[behavior.group-drag]
on       = "canvas"
initial  = "idle"
priority = 45
doc      = "Drags a group by its label."

[behavior.group-drag.states.idle]
on.pointer_down = { when = "hit != null && hit.kind == 'label'", to = "dragging", do = ["capture()", "select(hit.node)", "set('moves', 0)"] }
on.key = [
  { when = "key == 'G' && mods.mod", do = ["commit('group.requested', {'keys': selection})"] },
]

[behavior.group-drag.states.dragging]
entry             = ["raise(hit.node)"]
exit              = "commit('group.left', {'moves': vars.moves})"
on.pointer_move   = { do = ["move(hit.node, delta.world)", "set('moves', vars.moves + 1)"] }
on.pointer_up     = { to = "idle", do = ["commit('group.moved', {'key': hit.node, 'dx': travel.world.x, 'dy': travel.world.y})"] }
on.pointer_cancel = { to = "idle" }

[behavior.group-drag.actions.nudge]
doc  = "Moves the group by a little."
when = "node.kind == 'label'"
args = { dx = "f64", dy = "f64" }
do   = ["move(node.key, {'x': args.dx, 'y': args.dy})"]

[behavior.group-drag.actions.dissolve]
effect = "irreversible"
answer = "yes"
when   = "node.kind == 'label'"
do     = ["remove(node.key)"]
)toml";

// Every mistake on its line, and a hint.
constexpr std::string_view misspelt = R"toml([behavior.bad]
on = "canvas"
initial = "idel"
prority = 3
[behavior.bad.states.idle]
on.pointer_dwn = { do = ["capture()"] }
on.pointer_down = { when = "hti.kind == 'x'", to = "draging", do = ["mvoe(hit.node, delta.world)", "capture(1)"] }
on.tap = { do = ["capture()", "select(hit.node"] }
[behavior.bad.states.dragging]
[behavior.bad.actions.zap]
effect = "irreversible"
do = ["remove(node.key)"]
)toml";

bool mentions(const world::BehaviorProblems& ps, int line, std::string_view text) {
    return std::ranges::any_of(ps, [&](const world::BehaviorProblem& p) { return p.line == line && (p.message + " " + p.hint).contains(text); });
}

// A deterministic generator (the same fuzz everywhere).
struct Rng {
    std::uint64_t s = 0x2545f4914f6cdd1dull;
    std::uint64_t next() {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return s;
    }
    float unit() { return static_cast<float>(next() >> 40) / static_cast<float>(1ull << 24); }
    template <class T>
    const T& pick(std::span<const T> v) { return v[next() % v.size()]; }
};

// What a run left: every node's rectangle and the edges, as text.
std::string state_of(const world::Scene& sc, const std::vector<std::string>& selection) {
    std::string out;
    sc.each([&](world::NodeRef n) {
        const auto r = sc.rect(n);
        out += std::format("{} {} {} {} {};", sc.key(n), r.x, r.y, r.width, r.height);
    });
    for (const auto& e : sc.edges()) out += std::format("{}>{};", e.from, e.to);
    for (const auto& k : selection) out += "*" + k;
    return out;
}

// A behaviour's conformance (D-65: every plugin kind has a CTS): under
// seeded random input — presses, drags, clicks with every modifier, keys,
// the wheel — nothing fails, the result is the same every run, what it
// states passes spec §3.2's checks and every action it offers can be called
// with arguments of its declared types; and when the canvas lets it go, the
// images it made go with it.
std::vector<std::string> conformance(std::string_view behavior) {
    std::vector<std::string> problems;
    std::array<std::string, 2> runs;
    for (int run = 0; run < 2; ++run) {
        auto& sc = board("cts");
        sc.add({.kind = "label", .x = -300, .y = -200, .width = 120, .height = 30, .key = "g1"});
        std::vector<std::string> selection;
        auto names = signal(std::vector<std::string>{std::string(behavior)});
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board",
                                        .behaviors = names,
                                        .scene = "cts",
                                        .on_selection_changed = [&](std::vector<std::string> k) { selection = std::move(k); },
                                        .width = 800,
                                        .height = 600})}));
        h.settle();
        const std::size_t images = h.runtime().images().size();
        Rng rng;
        constexpr std::array<Modifier, 5> mods{Modifier::none, Modifier::shift, Modifier::alt, Modifier::ctrl, Modifier::meta};
        constexpr std::array<Key, 6> keys{Key::escape, Key::delete_, Key::backspace, Key::a, Key::g, Key::left};
        for (int i = 0; i < 120; ++i) {
            const Point a{rng.unit() * 800, rng.unit() * 600}, b{rng.unit() * 800, rng.unit() * 600};
            const Modifiers m = rng.pick(std::span<const Modifier>(mods));
            switch (rng.next() % 5) {
                case 0: click(h, a, m); break;
                case 1:
                case 2: drag(h, a, b, m); break;
                case 3: h.key(rng.pick(std::span<const Key>(keys)), m); h.step(); break;
                default: h.scroll(a, {0, rng.unit() * 60 - 30}, false); h.step(); break;
            }
            // Something taken is released by the end of a gesture: a cancel mid-drag too.
            if (i % 17 == 0) {
                h.step({press(a, m)});
                h.step({drag_to(b, m)});
                h.step({PointerEvent{.kind = PointerKind::cancel, .position = b}});
            }
        }
        h.settle();
        // What it states.
        std::size_t called = 0;
        std::vector<std::string> ids;
        // Every node, wherever the fuzz left the camera (a node off screen is a query away).
        sc.each([&](world::NodeRef n) { ids.push_back(std::format("main/board/{}", sc.key(n))); });
        for (const std::string& id : ids) {
            const auto n = h.tree().node(id);
            if (!n) {
                problems.push_back(std::format("{}: {} is not in the semantic tree", behavior, id));
                continue;
            }
            for (const auto& v : spec::check(*n)) problems.push_back(std::format("{}: {} {}", behavior, v.clause, v.message));
            for (const auto& a : n->actions) {
                if (a.name == "focus") continue;
                Value args;
                for (const auto& [name, type] : a.args) {
                    if (type == "string") args.set(name, Value(id.ends_with("/a") ? "b" : "a"));
                    else if (type == "bool") args.set(name, Value(true));
                    else args.set(name, Value(40.0));
                }
                Invocation call;
                call.args = args;
                call.actor = {"agent", "cts", {}};
                call.answer = a.answer;
                const auto e = h.tree().act(id, a.name, call);
                h.step();
                if (!e.exit_code()) problems.push_back(std::format("{}: {} on {} answered nothing", behavior, a.name, id));
                ++called;
            }
        }
        runs[static_cast<std::size_t>(run)] = state_of(sc, selection);
        if (run == 0) std::println("    {}: {} nodes left, {} stated, {} actions called", behavior, sc.size(), ids.size(), called);
        // Let go: its images go.
        names.set({});
        h.settle();
        if (h.runtime().images().size() > images) problems.push_back(std::format("{}: {} images left after it was let go", behavior, h.runtime().images().size() - images));
    }
    if (runs[0] != runs[1]) problems.push_back(std::format("{}: two runs of the same input differ", behavior));
    return problems;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.canvas_kit"};
    register_canvas_component();
    register_canvas_kit();

    s.test("select: a click chooses, ⇧ adds and takes out, the background clears, a ⇧-drag draws a marquee, Esc and Ctrl-A", [] {
        board("select");
        Fixture f("select", {"select"});
        auto& h = f.h;
        click(h, screen(50, 40));
        check_eq(f.selection, std::vector<std::string>{"a"});
        click(h, screen(250, 40), Modifier::shift);
        check_eq(f.selection, (std::vector<std::string>{"a", "b"}));
        click(h, screen(50, 40), Modifier::shift);
        check_eq(f.selection, std::vector<std::string>{"b"});
        click(h, screen(300, 200));
        check(f.selection.empty(), "a click on the background clears");
        // ⇧-drag from the background over a and c (not b).
        drag(h, screen(-300, -50), screen(20, 200), Modifier::shift);
        check_eq(f.selection, (std::vector<std::string>{"a", "c"}));
        h.key(Key::escape);
        h.step();
        check(f.selection.empty(), "Esc clears");
        h.key(Key::a, Modifier::ctrl);
        h.step();
        check_eq(f.selection.size(), std::size_t{3});
        // Agents: select adds, deselect takes out.
        h.key(Key::escape);
        h.step();
        check(has(action_names(h, "main/board/b"), "select"), "an unchosen node offers select");
        check_eq(act(h, "main/board/b", "select"), std::int64_t{0});
        check_eq(act(h, "main/board/c", "select"), std::int64_t{0});
        check_eq(f.selection, (std::vector<std::string>{"b", "c"}));
        check(has(action_names(h, "main/board/b"), "deselect"), "a chosen node offers deselect");
        check_eq(act(h, "main/board/b", "deselect"), std::int64_t{0});
        check_eq(f.selection, std::vector<std::string>{"c"});
        const auto c = h.tree().node("main/board/c");
        check(c && c->states.has(State::selected), "the semantic tree states it");
        // A plain drag on the background pans: the selection stays.
        drag(h, screen(300, 200), screen(250, 200));
        check_eq(f.selection, std::vector<std::string>{"c"});
    });

    s.test("select:marquee: a plain drag on the background draws the marquee", [] {
        board("marquee");
        Fixture f("marquee", {"select:marquee"});
        drag(f.h, screen(-10, -10), screen(120, 100));
        check_eq(f.selection, std::vector<std::string>{"a"});
    });

    s.test("drag: moves the node and the selection with it, Esc puts them back, move_to for agents", [] {
        auto& sc = board("drag");
        Fixture f("drag", {"select", "drag"});
        auto& h = f.h;
        drag(h, screen(50, 40), screen(80, 60));
        check_eq(f.selection, std::vector<std::string>{"a"});
        check_eq(sc.rect(*sc.find("a")).x, 30.0);
        check_eq(sc.rect(*sc.find("a")).y, 20.0);
        check(f.saw("moved"), "moved is told");
        // Two chosen: dragging one moves both.
        click(h, screen(250, 40), Modifier::shift);
        check_eq(f.selection.size(), std::size_t{2});
        drag(h, screen(250, 40), screen(250, 140));
        check_eq(sc.rect(*sc.find("a")).y, 120.0);
        check_eq(sc.rect(*sc.find("b")).y, 100.0);
        // A click on one of them: that one alone.
        click(h, screen(250, 140));
        check_eq(f.selection, std::vector<std::string>{"b"});
        // Esc mid-drag: back where it was, nothing told.
        const std::size_t told = f.events.size();
        h.step({press(screen(250, 140))});
        h.step({drag_to(screen(320, 200))});
        check_eq(sc.rect(*sc.find("b")).x, 270.0);
        h.key(Key::escape);
        h.step({release(screen(320, 200))});
        check_eq(sc.rect(*sc.find("b")).x, 200.0);
        check_eq(sc.rect(*sc.find("b")).y, 100.0);
        check_eq(f.events.size(), told);
        // A press that does not travel moves nothing.
        click(h, screen(-220, 180));
        check_eq(sc.rect(*sc.find("c")).x, -250.0);
        // Agents.
        check(has(action_names(h, "main/board/c"), "move_to"), "drag gives nodes move_to");
        check_eq(act(h, "main/board/c", "move_to", Value{}.set("x", Value(5.0)).set("y", Value(6.0))), std::int64_t{0});
        check_eq(sc.rect(*sc.find("c")).x, 5.0);
        check(act(h, "main/board/c", "move_to", Value{}.set("x", Value(5.0))) != 0, "move_to without y is refused");
    });

    s.test("drag: a kind with a title bar is dragged by it; its body is not", [] {
        auto& sc = world::scene("title");
        sc.clear();
        sc.kind("window", {.title_bar = 20});
        sc.add({.kind = "window", .x = 0, .y = 0, .width = 200, .height = 150, .key = "w", .fill = Color::hex(0xe2e8f0)});
        Fixture f("title", {"drag"});
        drag(f.h, screen(100, 80), screen(150, 80));  // the body: pans the camera
        check_eq(sc.rect(*sc.find("w")).x, 0.0);
        f.h.settle();
        // Wherever the pan (and its glide) left it, its bar drags it.
        const auto w = f.h.tree().node("main/board/w");
        check(w && w->bounds, "the window is on screen");
        const auto b = *w->bounds;
        drag(f.h, {b.x + 100, b.y + 10}, {b.x + 100, b.y + 70});
        check_eq(sc.rect(*sc.find("w")).y, 60.0);
    });

    s.test("snap: edges and centres meet the neighbours' within 6 px, with guides; snap:20 meets a grid", [] {
        auto& sc = board("snap");
        Fixture f("snap", {"drag", "snap"});
        auto& h = f.h;
        // a's top 4 px below b's, mid-drag: it meets it, and a guide (red)
        // runs along the line between them.
        h.step({press(screen(50, 40))});
        h.step({drag_to(screen(52, 42))});
        h.step({drag_to(screen(54, 44))});
        check_eq(sc.rect(*sc.find("a")).y, 0.0);
        check_eq(sc.rect(*sc.find("a")).x, 4.0);
        const auto guide = h.pixels().at(550, 300);
        check(guide.r > 200 && guide.g < 80, std::format("a guide between a and b: {} {} {}", guide.r, guide.g, guide.b));
        h.step({release(screen(54, 44))});
        const auto after = h.pixels().at(550, 300);
        check(after.g > 200, "no guide once it is dropped");
        // Its right edge 0 px from b's left: meets it; the y meets nothing.
        drag(h, screen(54, 40), screen(150, 70));
        check_eq(sc.rect(*sc.find("a")).x, 100.0);
        check_eq(sc.rect(*sc.find("a")).y, 30.0);
        // snap:20 — the grid's lines too.
        auto& g = board("grid-snap");
        Fixture f2("grid-snap", {"drag", "snap:20"});
        drag(f2.h, screen(50, 40), screen(50 + 36, 40 + 43));  // a would land at (36, 43): the grid's (40, 40)
        check_eq(g.rect(*g.find("a")).x, 40.0);
        check_eq(g.rect(*g.find("a")).y, 40.0);
    });

    s.test("resize: handles on the one chosen node, ⇧ keeps proportions, never below 8 px; resize for agents", [] {
        auto& sc = board("resize");
        Fixture f("resize", {"select", "drag", "resize"});
        auto& h = f.h;
        click(h, screen(50, 40));
        // The bottom-right handle at (100, 80).
        drag(h, screen(100, 80), screen(160, 100));
        auto r = sc.rect(*sc.find("a"));
        check_eq(r.width, 160.0);
        check_eq(r.height, 100.0);
        check(f.saw("resized"), "resized is told");
        // The left side, past the right: stops 8 px short.
        drag(h, screen(0, 50), screen(400, 50));
        r = sc.rect(*sc.find("a"));
        check_eq(r.width, 8.0);
        check_eq(r.right(), 160.0);
        // ⇧ on a corner: 2:1 kept.
        check_eq(act(h, "main/board/a", "resize", Value{}.set("width", Value(100.0)).set("height", Value(50.0))), std::int64_t{0});
        r = sc.rect(*sc.find("a"));
        drag(h, screen(r.right(), r.bottom()), screen(r.right() + 100, r.bottom() + 10), Modifier::shift);
        r = sc.rect(*sc.find("a"));
        check_eq(r.width, 200.0);
        check_eq(r.height, 100.0);
        check(act(h, "main/board/a", "resize", Value{}.set("width", Value(-1.0)).set("height", Value(10.0))) != 0, "a size below 0 is refused");
    });

    s.test("connect: ⌥-drag from a node to another; connect and disconnect for agents", [] {
        auto& sc = board("connect");
        Fixture f("connect", {"drag", "connect"});
        auto& h = f.h;
        drag(h, screen(50, 40), screen(250, 40), Modifier::alt);
        check_eq(sc.edges().size(), std::size_t{1});
        check_eq(sc.edges()[0].from, std::string("a"));
        check_eq(sc.edges()[0].to, std::string("b"));
        check_eq(sc.rect(*sc.find("a")).x, 0.0);  // ⌥ connects, it does not move
        // Dropped on the background: nothing.
        drag(h, screen(50, 40), screen(50, 250), Modifier::alt);
        check_eq(sc.edges().size(), std::size_t{1});
        check_eq(act(h, "main/board/c", "connect", Value{}.set("to", Value("a"))), std::int64_t{0});
        check_eq(sc.edges_of("a").size(), std::size_t{2});
        check(act(h, "main/board/c", "connect", Value{}.set("to", Value("nobody"))) != 0, "an unknown node is refused");
        check(has(action_names(h, "main/board/c"), "disconnect"), "a connected node offers disconnect");
        check_eq(act(h, "main/board/a", "disconnect", Value{}.set("to", Value("c"))), std::int64_t{0});
        check_eq(sc.edges_of("a").size(), std::size_t{1});
        check(f.saw("connected") && f.saw("disconnected"), "both told");
    });

    s.test("delete: Delete removes the selection; the action is irreversible and needs its answer", [] {
        auto& sc = board("delete");
        Fixture f("delete", {"select", "delete"});
        auto& h = f.h;
        click(h, screen(50, 40));
        click(h, screen(250, 40), Modifier::shift);
        h.key(Key::delete_);
        h.step();
        check_eq(sc.size(), std::size_t{1});
        check(f.selection.empty(), "the deleted are no longer chosen");
        check(f.saw("deleted"), "deleted is told");
        check(act(h, "main/board/c", "delete") != 0, "without its answer, refused");
        check_eq(sc.size(), std::size_t{1});
        check_eq(act(h, "main/board/c", "delete", {}, "yes"), std::int64_t{0});
        check_eq(sc.size(), std::size_t{0});
    });

    s.test("grid and minimap: a cell of 16–32 px at any zoom, every second line stronger; the minimap moves the view", [] {
        world::scene("grid").clear();
        for (double z : {0.0123, 0.37, 1.0, 5.5, 140.0}) {
            Headless h({.size = {800, 600}});
            h.mount("main", column({.width = 800, .height = 600},
                                   {canvas({.id = "board", .behaviors = std::vector<std::string>{"grid"}, .max_zoom = 1e4, .min_zoom = 1e-4, .scene = "grid", .zoom = z, .width = 800, .height = 600})}));
            h.settle();
            // The lines along a row (one between the horizontal lines): a cell
            // apart, or two while the finer ones are still too faint to see.
            const raster::Surface& px = h.pixels();
            std::vector<int> lines;
            for (int row = 10; row < 60; ++row) {
                std::vector<int> found;
                for (int x = 0; x < 800; ++x)
                    if (px.at(x, row).r < 246) found.push_back(x);
                if (lines.empty() || found.size() < lines.size()) lines = std::move(found);
            }
            check(lines.size() >= 24, std::format("zoom {}: {} lines", z, lines.size()));
            if (lines.size() >= 2) {
                const double spacing = static_cast<double>(lines.back() - lines.front()) / static_cast<double>(lines.size() - 1);
                check(spacing >= 15.5 && spacing <= 33, std::format("zoom {}: lines {} px apart", z, spacing));
            }
        }
        board("mini");
        auto x = signal(0.0), y = signal(0.0);
        Headless h({.size = {800, 600}});
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board", .behaviors = std::vector<std::string>{"minimap"}, .scene = "mini", .x = x, .y = y, .width = 800, .height = 600})}));
        h.settle();
        // The minimap: the bottom-right 200 × 136, 12 px in. Its centre is the
        // view's (the scene is inside the view); its top-left is up and left.
        const Point inside{800 - 12 - 180, 600 - 12 - 116};
        check(h.pixels().at(static_cast<int>(inside.x), static_cast<int>(inside.y)).r >= 240, "the panel is drawn");
        // a (green, 100 × 80 at the origin) in it: the view's 800 × 600 fit into 188 × 124.
        const auto green = h.pixels().at(698, 528);
        check(green.g > green.r + 40 && green.g > green.b + 40, std::format("a is green in the minimap: {} {} {}", green.r, green.g, green.b));
        h.step({press(inside)});
        h.step({release(inside)});
        check(x.get() < -100 && y.get() < -100, std::format("the view moved up and left: {}, {}", x.get(), y.get()));
    });

    s.test("declarative: a statechart from TOML drags a group by its label, commits, and adds its actions", [] {
        const auto loaded = world::load_behaviors(group_drag, "group-drag.behavior.omui.toml");
        check(loaded.has_value(), loaded ? std::string{} : loaded.error().front().text());
        check_eq(loaded.value_or(std::vector<std::string>{}), std::vector<std::string>{"group-drag"});
        check(has(world::behaviors(), "group-drag"), "registered by name");
        check_eq(world::behavior_operations().size(), std::size_t{17});
        auto& sc = board("group");
        sc.add({.kind = "label", .x = -300, .y = -200, .width = 120, .height = 30, .key = "g1", .fill = Color::hex(0xfacc15)});
        std::vector<Value> told;
        std::vector<std::string> chosen;
        Headless h({.size = {800, 600}});
        auto keep = [&](std::string_view name) { return std::pair<std::string, Handler>{std::string(name), [&told, n = std::string(name)](const Value& v) { told.push_back(Value{}.set("event", Value(n)).set("args", v)); }}; };
        h.mount("main", column({.width = 800, .height = 600},
                               {canvas({.id = "board",
                                        .behaviors = std::vector<std::string>{"select", "group-drag"},
                                        .scene = "group",
                                        .on_selection_changed = [&](std::vector<std::string> keys) { chosen = std::move(keys); },
                                        .width = 800,
                                        .height = 600,
                                        .on = {keep("group.moved"), keep("group.left"), keep("group.requested")}})}));
        h.settle();
        // The label: dragged, chosen, raised, told.
        drag(h, screen(-250, -190), screen(-210, -160));
        const auto r = sc.rect(*sc.find("g1"));
        check_eq(r.x, -260.0);
        check_eq(r.y, -170.0);
        check_eq(chosen, std::vector<std::string>{"g1"});
        check_eq(told.size(), std::size_t{2});
        if (told.size() == 2) {
            check_eq(std::string(told[0].get("event")->as_string()), std::string("group.left"));
            check_eq(told[0].get("args")->get("moves")->as_int(), std::int64_t{4});
            check_eq(std::string(told[1].get("event")->as_string()), std::string("group.moved"));
            check_eq(told[1].get("args")->get("dx")->as_double(), 40.0);
        }
        // A box is not a label: the press goes on (select chooses it on release).
        click(h, screen(50, 40));
        check_eq(chosen, std::vector<std::string>{"a"});
        check_eq(sc.rect(*sc.find("a")).x, 0.0);
        // Keys.
        h.key(Key::g, Modifier::ctrl);
        h.step();
        check(told.size() == 3 && told[2].get("args")->get("keys")->list()->size() == 1, "the key transition commits");
        // Its actions, where `when` holds.
        check(has(action_names(h, "main/board/g1"), "nudge"), "a label offers nudge");
        check(!has(action_names(h, "main/board/a"), "nudge"), "a box does not");
        check_eq(act(h, "main/board/g1", "nudge", Value{}.set("dx", Value(10.0)).set("dy", Value(-5.0))), std::int64_t{0});
        check_eq(sc.rect(*sc.find("g1")).x, -250.0);
        check(act(h, "main/board/g1", "nudge", Value{}.set("dx", Value(10.0))) != 0, "a missing argument is refused");
        check(act(h, "main/board/g1", "dissolve") != 0, "irreversible: refused without its answer");
        check_eq(act(h, "main/board/g1", "dissolve", {}, "yes"), std::int64_t{0});
        check(!sc.find("g1"), "dissolved");
    });

    s.test("declarative: a misspelt file is refused whole, each problem on its line with a hint", [] {
        const auto bad = world::load_behaviors(misspelt, "bad.behavior.omui.toml");
        check(!bad.has_value(), "refused");
        if (bad) return;
        const auto& ps = bad.error();
        for (const auto& p : ps) std::println("    {}", p.text());
        check(mentions(ps, 3, "did you mean `idle`?"), "the initial state");
        check(mentions(ps, 4, "did you mean `priority`?"), "a key");
        check(mentions(ps, 6, "did you mean `pointer_down`?"), "an event");
        check(mentions(ps, 7, "did you mean `hit`?"), "a name");
        check(mentions(ps, 7, "did you mean `dragging`?"), "a state");
        check(mentions(ps, 7, "did you mean `move`?"), "an operation");
        check(mentions(ps, 7, "takes 0 arguments"), "an arity");
        check(mentions(ps, 8, "pointer_down transition"), "capture outside a press");
        check(mentions(ps, 8, "ends early"), "a parse error");
        check(mentions(ps, 10, "S-1"), "an irreversible action without its answer");
        check(!has(world::behaviors(), "bad"), "nothing registered");
        const auto broken = world::load_behaviors("[behavior.x\non = 1", "broken.behavior.omui.toml");
        check(!broken && broken.error().front().line == 1, "malformed TOML: where");
        const auto missing = world::load_behavior_file("/nonexistent/x.behavior.omui.toml");
        check(!missing.has_value(), "a missing file");
        // From a file: the same as from text, the problems naming the file.
        const auto dir = std::filesystem::temp_directory_path() / std::format("omui-canvas-kit-{}", std::random_device{}());
        std::filesystem::create_directories(dir);
        {
            std::ofstream(dir / "group-drag.behavior.omui.toml", std::ios::binary) << group_drag;
            std::ofstream(dir / "bad.behavior.omui.toml", std::ios::binary) << misspelt;
        }
        const auto good = world::load_behavior_file(dir / "group-drag.behavior.omui.toml");
        check(good && good->size() == 1, "a file loads");
        const auto wrong = world::load_behavior_file(dir / "bad.behavior.omui.toml");
        check(!wrong && wrong.error().front().file.ends_with("bad.behavior.omui.toml") && wrong.error().front().line == 3, "a file's problems name it");
        std::filesystem::remove_all(dir);
    });

    s.test("conformance: every behaviour registered — canvas-kit's and a declarative one — passes the behaviour CTS", [] {
        check(world::load_behaviors(group_drag, "group-drag.behavior.omui.toml").has_value(), "group-drag loads");
        std::vector<std::string> all;
        for (const std::string& name : world::behaviors()) all.push_back(name);
        // With arguments, as canvases name them.
        for (const char* more : {"select:marquee", "snap:grid", "snap:20", "grid:25", "minimap:top-left"}) all.emplace_back(more);
        check(all.size() >= 14, std::format("{} behaviours", all.size()));
        for (const std::string& b : all)
            for (const std::string& p : conformance(b)) check(false, p);
    });

    return s.run(argc, argv);
}
