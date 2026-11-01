// The site as a reader, an agent and a crawler meet it: each place shown in
// turn, in both languages; the components with their code and the style packs; the lens moving the same page from its pixels to
// the tree's wireframe, the JSON an agent reads and the assertions a test
// writes; the home page's tree hash pinned (what the footer compares in the
// browser); the live timeline editor edited and undone; the static HTML a
// reader without JavaScript gets, written into dist-toy's page.
import std;
import openmui;
import openmui.site;
import openmui.ui.markdown;  // openmui.markdown with it
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct Site {
    Headless h;
    explicit Site(std::string_view place = "home", std::string_view lang = "en") : h{HeadlessOptions{.size = {1280, 800}}} {
        AppSpec spec = site::app(place, lang);
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle();
    }
    Value snapshot(std::string_view place) { return h.tree().snapshot({.root = std::format("main/body/view/{}/page", place)}).to_value(); }
    std::string text(std::string_view id) {
        const auto n = h.tree().node(id);
        return n ? n->name : std::string();
    }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
    // The pixels of one node's box (the view, say).
    std::uint64_t pixels_of(std::string_view id) { return pixels_in(h.runtime().bounds(h.runtime().find(id))); }
    // The page above its foot: what it shows, not the history of its time the foot draws.
    std::uint64_t page() { return pixels_in({0, 0, 1280, h.runtime().bounds(h.runtime().find("main/foot")).y}); }
    // A region's pixels, the bar's frame meter left out: a live readout of
    // the frames shown so far, as the foot's strip is of the page's time.
    std::uint64_t pixels_in(Rect r) {
        const auto& px = h.pixels();
        const int x0 = std::max(0, static_cast<int>(r.x)), x1 = std::min(px.width(), static_cast<int>(r.x + r.width));
        const Rect m = h.runtime().bounds(h.runtime().find("main/bar/frames"));
        const int mx0 = std::clamp(static_cast<int>(std::floor(m.x)), x0, x1), mx1 = std::clamp(static_cast<int>(std::ceil(m.x + m.width)), mx0, x1);
        const int my0 = static_cast<int>(std::floor(m.y)), my1 = static_cast<int>(std::ceil(m.y + m.height));
        Hash hash;
        for (int y = std::max(0, static_cast<int>(r.y)); y < std::min(px.height(), static_cast<int>(r.y + r.height)); ++y) {
            const auto part = [&](int from, int to) { hash.bytes(std::as_bytes(std::span(px.row(y) + from, static_cast<std::size_t>(std::max(0, to - from))))); };
            if (y >= my0 && y < my1) {
                part(x0, mx0);
                part(mx1, x1);
            } else part(x0, x1);
        }
        return std::stoull(hash.hex(), nullptr, 16);
    }
    double value(std::string_view id, std::string_view field = "value") {
        const auto n = h.tree().node(id);
        return n && n->value.get(field) ? n->value.get(field)->as_double(-1) : -1;
    }
    bool visible(std::string_view id) {
        const auto n = h.tree().node(id);
        return n && !n->states.has(State::hidden);
    }
    // A place is shown when it is the one built.
    bool shown(std::string_view place) { return h.tree().node(std::format("main/body/view/{}/page", place)).has_value(); }
};

std::uint64_t fnv(std::string_view s) { return hash::fnv1a(s); }

constexpr std::string_view page = R"(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>openmui</title>
</head>
<body>
<canvas id="canvas" tabindex="-1"></canvas>
<div id="status">Loading…</div>
<script>var omuiFonts = [["Inter.ttf", "assets/4304c4ca3090.ttf"]];
var Module = { canvas: document.getElementById('canvas'), onRuntimeInitialized: function () { document.getElementById('status').remove(); } };</script>
<script src="omui-site.js"></script>
</body>
</html>
)";

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site"};
    s.test("every place has its document in both languages; the content is what the docs say", [] {
        for (const site::Place& p : site::places()) {
            if (p.id == "timeline" || p.id == "components" || p.id == "hello") {
                check(site::document(p.id, "en").empty(), std::format("{} is a live page, not a document", p.id));
                continue;
            }
            for (std::string_view lang : {"en", "zh"}) {
                const std::string_view doc = site::document(p.id, lang);
                // Home's title is the opening's claim (hero.cpp); its document begins a level down.
                check(doc.starts_with(p.id == "home" ? "## " : "# "), std::format("{} in {}: a document with its title", p.id, lang));
                check(!md::parse(doc).root.children.empty(), std::format("{} in {}: reads as Markdown", p.id, lang));
            }
        }
        check(site::document("home", "zh").find("一棵树") != std::string_view::npos, "the Chinese home page");
        check(site::document("nowhere", "en").empty());
    });

    s.test("each place shown in turn: one at a time, its tree under the view", [] {
        Site a;
        check(a.shown("home"), "home first");
        check(!a.shown("features"), "one place at a time");
        for (const site::Place& p : site::places()) {
            check(a.h.click(std::format("main/bar/nav/to_{}", p.id)), std::format("the nav's {}", p.id));
            a.h.settle();
            for (const site::Place& q : site::places())
                check(a.shown(q.id) == (p.id == q.id), std::format("{} shown: {}", p.id, q.id));
            const std::size_t nodes = a.snapshot(p.id).get("nodes")->list()->size();
            check(nodes >= 3u, std::format("{}: its tree ({} nodes)", p.id, nodes));
        }
    });

    s.test("the language: the bar, the nav and the documents in Chinese, the same ids", [] {
        Site a;
        std::vector<std::string> ids;
        for (const auto& n : a.h.tree().snapshot({}).nodes) ids.push_back(n.id);
        check_eq(a.text("main/bar/nav/to_features"), std::string("Features"));
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check_eq(a.text("main/bar/nav/to_features"), std::string("特性"));
        check_eq(a.text("main/bar/lang"), std::string("English"));
        const std::string doc = a.snapshot("home").json();
        check(doc.find("一棵树") != std::string::npos, "the Chinese home page");
        // The ids are the structure's, not the text's: the same nodes in Chinese.
        std::vector<std::string> zh;
        for (const auto& n : a.h.tree().snapshot({}).nodes) zh.push_back(n.id);
        std::set<std::string> was(ids.begin(), ids.end());
        std::size_t kept = 0;
        for (const auto& id : zh) kept += was.contains(id);
        check(kept * 10 >= zh.size() * 9, std::format("{} of {} ids the same", kept, zh.size()));
    });

    s.test("switching the language: the letters gather into the other language, and end as the page drawn in it", [] {
        Site a, zh("home", "zh");
        zh.h.move({4, 796});
        zh.h.settle();
        const auto direct = zh.page();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(0.12));
        const auto moving = a.page();
        check(moving != direct, "under way: the letters still gathering");
        check(site::tree_hash(a.snapshot("home")) == site::tree_hash(zh.snapshot("home")), "the tree is already the other language's: the motion is only paint");
        a.h.settle(seconds(3));
        (void)a.h.save_png(scratch_dir("site-sw") / "a.png");
        check(a.page() == direct, "at rest: the very pixels of the page opened in Chinese (above the foot, where the switch is marked)");
    });

    s.test("the opening: a checklist as a person sees it, and its node as a screen reader says it, an agent sends it, a test asserts it", [] {
        Site a;
        const std::string hero = "main/body/view/home/page/hero";
        const auto ship = a.h.tree().node(hero + "/demo/specimen/ship");
        check(ship.has_value() && ship->role == Role::button && ship->states.has(State::disabled), "Ship 0.1 waits for its checks");
        const std::string panel = hero + "/demo/renderings/";
        check_eq(a.text(panel + "reader/body/line"), std::string("\"Ship 0.1\", button, dimmed"));
        check(a.text(panel + "agent/body/line").find("\"act\": \"specimen/ship\", \"action\": \"activate\"") != std::string::npos, a.text(panel + "agent/body/line"));
        check(a.text(panel + "test/body/line").find("check(node(\"specimen/ship\").states.has(\"disabled\"));") != std::string::npos, a.text(panel + "test/body/line"));
        // A person checks one: the panels follow it, read again from the tree.
        check(a.h.click(hero + "/demo/specimen/items/docs"));
        a.h.advance(seconds(0.5));
        check_eq(a.text(panel + "reader/body/line"), std::string("\"Docs reviewed\", checkbox, checked"));
        check(a.text(panel + "test/body/line").find("check(node(\"specimen/items/docs\").states.has(\"checked\"));") != std::string::npos,
              a.text(panel + "test/body/line"));
    });

    s.test("the opening's agent ships it: each step a real action as an agent, through the same checks, each one undoable", [] {
        Site a;
        const std::string hero = "main/body/view/home/page/hero";
        check(a.h.click(hero + "/agent_row/let_agent"), "a person asks the agent");
        a.h.advance(seconds(6));
        for (const char* item : {"tests", "docs", "notes"}) {
            const auto n = a.h.tree().node(hero + "/demo/specimen/items/" + item);
            check(n && n->states.has(State::checked), std::format("{} checked by the agent", item));
        }
        check(a.text(hero + "/demo/specimen/shipped").starts_with("Shipped"), "shipped");
        check(a.text(hero + "/demo/renderings/agent/body/line").find("done") != std::string::npos, a.text(hero + "/demo/renderings/agent/body/line"));
        const auto agents = std::ranges::count_if(a.h.runtime().action_journal(), [](const ActionEntry& e) { return e.actor.starts_with("agent"); });
        check(agents >= 4, std::format("{} actions journaled as the agent's", agents));
        // Every step can be taken back, the shipping too: undo unships it, the panels with it; redo ships it again.
        const auto shipped = [&] { return a.text(hero + "/demo/specimen/shipped").starts_with("Shipped"); };
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(!shipped(), "undone: not shipped");
        check(a.text(hero + "/demo/renderings/agent/body/line").find("done") == std::string::npos, "the agent's request is past");
        const auto items_checked = [&] {
            int n = 0;
            for (const char* item : {"tests", "docs", "notes"})
                if (auto x = a.h.tree().node(hero + "/demo/specimen/items/" + item); x && x->states.has(State::checked)) ++n;
            return n;
        };
        check_eq(items_checked(), 3);  // the checks stay: the shipping alone was taken back
        check_eq(a.h.runtime().redo({}).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(shipped(), "redone: shipped");
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(!shipped() && items_checked() == 2, "then the last check");
    });

    s.test("on a phone: the bar wraps (the places on a line of their own), the place the whole width, a panel in its place", [] {
        Headless h{HeadlessOptions{.size = {390, 844}}};
        AppSpec spec = site::app("home", "zh");
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.settle();
        const auto shown = [&](std::string_view id) {
            const auto n = h.tree().node(id);
            return n && !n->states.has(State::hidden) && !n->states.has(State::offscreen);
        };
        check(shown("main/bar/nav/to_features") && shown("main/foot/lens_tree"), "the places in the bar, the lenses at the foot");
        const Rect nav = h.runtime().screen_bounds(h.runtime().find("main/bar/nav"));
        check(nav.x >= 0 && nav.x + nav.width <= 390.5f, std::format("the places within the phone: {} + {}", nav.x, nav.width));
        const Rect view = h.runtime().screen_bounds(h.runtime().find("main/body/view"));
        check(view.width == 390, std::format("the view the whole width: {}", view.width));
        // The renderings' words whole: no line of a card below its card's bottom.
        for (const char* card : {"reader", "agent", "test"}) {
            const std::string base = std::format("main/body/view/home/page/hero/demo/renderings/{}", card);
            check(h.runtime().valid(h.runtime().find(base + "/body/line")), base);
            const Rect c = h.runtime().screen_bounds(h.runtime().find(base));
            const Rect l = h.runtime().screen_bounds(h.runtime().find(base + "/body/line"));
            check(l.y + l.height <= c.y + c.height + 0.5f, std::format("{}'s line inside its card: {} vs {}", card, l.y + l.height, c.y + c.height));
        }
        for (const char* id : {"hero/demo/specimen/ship", "hero/agent_row/let_agent", "hero/cta/to_features"}) {
            const Rect r = h.runtime().screen_bounds(h.runtime().find(std::format("main/body/view/home/page/{}", id)));
            check(r.x >= 0 && r.x + r.width <= 390, std::format("{} inside: {} + {}", id, r.x, r.width));
        }
        // A panel takes the place's room; Esc puts it away, wherever the focus is.
        check(h.click("main/bar/settings_toggle"));
        h.settle();
        check(shown("main/body/settings") && !shown("main/body/view"), "the settings in the place's place");
        h.runtime().focus(NodeId{});
        h.key(Key::escape);
        h.settle();
        check(!shown("main/body/settings") && shown("main/body/view"), "Esc closed it, the place back");
        check(h.click("main/bar/nav/to_features"));
        h.settle();
        const auto features = h.tree().node("main/body/view/features/page/read/doc");
        check(features && !features->states.has(State::offscreen), "the features, read");
    });

    s.test("the settings: each choice's name drawn and read by it, and each one changing what it says — the theme, the motion, how things move, the effects", [] {
        Site a;
        check(a.h.click("main/bar/settings_toggle"));
        a.h.settle();
        const std::string at = "main/body/settings/";
        for (const std::string id : {"theme", "motion", "motion_style", "effects", "renderer"}) {
            const auto field = a.h.tree().node(at + id + "_field/" + id), name = a.h.tree().node(at + id + "_field/name");
            check(field && name && !name->name.empty() && field->name == name->name, std::format("{}: drawn as it is read", id));
        }
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        auto choose = [&](std::string_view id, std::string_view value) {
            agent.args = Value{}.set("value", std::string(value));
            check(a.h.tree().act(std::format("{}{}_field/{}", at, id, id), "set_value", agent).exit_code() == std::optional<int>(0), std::format("{} {}", id, value));
            a.h.settle(seconds(1));
        };
        const Color light = a.h.runtime().theme().color("background");
        check(std::abs(light.r - 0xfb / 255.0f) < 0.002f && std::abs(light.b - 0xfa / 255.0f) < 0.002f, "the site's own theme (site.theme.omui.toml), not the default it extends");
        choose("theme", "dark");
        const Color dark = a.h.runtime().theme().color("background");
        check(dark.r + dark.g + dark.b < light.r + light.g + light.b - 1, "dark");
        choose("motion", "off");
        check(a.h.runtime().motion_level() == MotionLevel::off);
        choose("motion_style", "ink");
        check(a.h.runtime().motion_choice() == "ink");
        choose("motion_style", "default");
        check(a.h.runtime().motion_choice().empty(), "each control's own again");
        choose("effects", "t1");
        check(a.h.runtime().adaptation().reason == "the person's setting", a.h.runtime().adaptation().reason);
        check(a.h.tree().act(at + "theme_field/theme", "set_value", [&] { agent.args = Value{}.set("value", "sepia"); return agent; }()).exit_code() != std::optional<int>(0),
              "not one of its choices");
    });

    s.test("the lens: pixels, the tree's wireframe, an agent's JSON, a test's assertions — of the same nodes", [] {
        Site a;
        const auto plain = a.page();
        check(!a.visible("main/body/view/wireframe") && !a.visible("main/body/view/agent"), "only the pixels");
        check(a.h.click("main/foot/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        check(a.visible("main/body/view/wireframe"), "the wireframe over the page");
        check(a.pixels() != plain, "the wireframe drawn");
        const auto wire = a.h.tree().node("main/body/view/wireframe");
        const std::int64_t drawn = wire->value.get("nodes") ? wire->value.get("nodes")->as_int() : 0;
        const auto home = a.snapshot("home");
        check(drawn == static_cast<std::int64_t>(home.get("nodes")->list()->size()), "every node of the place outlined");

        check(a.h.click("main/foot/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string json = a.text("main/body/view/agent/agent_text");
        check(json.starts_with("// openmui.tree@0.1's snapshot of main/body/view/home"), "the place named once");
        check(json.find("\"id\":\"…/page/doc/b0\"") != std::string::npos, "the nodes as an agent reads them, from the place");
        for (const Value& n : *home.get("nodes")->list()) {
            std::string id = std::string(n.get("id")->as_string());
            if (id.starts_with("main/body/view/home/")) id = "…/" + id.substr(std::string_view("main/body/view/home/").size());
            check(json.find(Value(id).json()) != std::string::npos, std::format("{} in the agent's JSON", id));
        }

        check(a.h.click("main/foot/lens_test"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string lines = a.text("main/body/view/test/test_text");
        check(lines.find("tree.node(std::format(\"main/body/view/home/{}\", path))") != std::string::npos, "the place's nodes found under it");
        check(lines.find("check_eq(node(\"page/doc/b0\").role, \"heading\");") != std::string::npos, "the title's role asserted");

        check(a.h.click("main/foot/lens_pixels"));
        a.h.move({4, 796});  // the pointer off the buttons (no hover)
        a.h.settle(seconds(1));
        check(a.page() == plain, "back to the pixels, the same pixels (above the foot, where the lenses are marked)");
    });

    s.test("the timeline's monitor: what plays at the playhead, the title fading as its clip does; Play runs the playhead and stops at the end", [] {
        Site a;
        check(a.h.click("main/bar/nav/to_timeline"));
        a.h.settle(seconds(3));
        const std::string base = "main/body/view/timeline/page/";
        check_eq(a.text(base + "program/monitor/time/row:0/timecode"), std::string("01:15"));
        check(a.h.click(base + "program/play"));
        a.h.advance(seconds(1));
        const std::string after = a.text(base + "program/monitor/time/row:0/timecode");
        check(after != "01:15" && after.starts_with("02:"), after);
        // Sought into the fight: the playhead is the editor's, the monitor follows it.
        Invocation agent;
        agent.actor = {"agent", "test", {}};
        agent.args = Value{}.set("frame", std::int64_t{150});
        check(a.h.tree().act(base + "editor", "seek", agent).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.1));
        check(a.text(base + "program/monitor/time/row:0/timecode").starts_with("05:"), a.text(base + "program/monitor/time/row:0/timecode"));
    });

    s.test("Build with it: the opening's first button scrolls the page to its three commands", [] {
        Site a;
        check(a.h.click("main/body/view/home/page/hero/cta/to_start"));
        a.h.settle(seconds(2));
        std::optional<spec::SemanticNode> heading;
        for (const auto& n : a.h.tree().snapshot({}).nodes)
            if (n.role == Role::heading && n.name == "Build with it") heading = n;
        const auto view = a.h.tree().node("main/body/view");
        check(heading && heading->bounds && view && view->bounds && heading->bounds->y < view->bounds->y + 150 && heading->bounds->y >= view->bounds->y,
              heading && heading->bounds && view && view->bounds ? std::format("the section at {} in a view from {}", heading->bounds->y, view->bounds->y) : "no section");
    });

    s.test("the home page's tree: pinned, the same in a browser as here; boxes do not count", [] {
        Site a;
        const auto h = site::tree_hash(a.snapshot("home"));
        std::println("    home tree {:016x}", h);
        check(h == site::pinned_home_hash, "the hash the footer compares");
        check(a.text("main/foot/proof/hash").find("✓") != std::string::npos, a.text("main/foot/proof/hash"));
        // A narrower window moves every box, scrolls further and leaves some of the page off screen: not the tree.
        Headless narrow{HeadlessOptions{.size = {820, 640}}};
        AppSpec spec = site::app();
        spec.setup(narrow.runtime());
        narrow.mount("main", spec.view());
        narrow.settle();
        check(site::tree_hash(narrow.tree().snapshot({.root = "main/body/view/home/page"}).to_value()) == h, "the same tree, another window");
        // The Chinese page is another tree.
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check(site::tree_hash(a.snapshot("home")) != h, "another language, another tree");
    });

    s.test("the live timeline editor: an agent's edit, the person's undo, the tree that follows", [] {
        Site a;
        check(a.h.click("main/bar/nav/to_timeline"));
        a.h.settle();
        constexpr std::string_view editor = "main/body/view/timeline/page/editor";
        const auto before = site::tree_hash(a.snapshot("timeline"));
        auto tracks = [&] { return a.h.tree().node(editor)->value.get("tracks")->json(); };
        const std::string was = tracks();
        Invocation agent;
        agent.actor = {"agent", "test", {}};
        agent.args = Value{}.set("track", 0).set("clip", 1).set("to_track", 0).set("frame", 100);
        check(a.h.tree().act(editor, "move", agent).exit_code().value_or(-1) == std::int64_t{0}, "an agent moves the fight");
        a.h.settle();
        check(site::tree_hash(a.snapshot("timeline")) != before, "the tree follows the edit");
        check(a.text("main/foot/proof/hash") != std::format("tree {:016x}", before), "and so does the footer");
        check(a.h.tree().act(editor, "undo", agent).exit_code().value_or(-1) == std::int64_t{0}, "undone in the editor's own history");
        a.h.settle();
        check(tracks() == was, "the very timeline again");
    });

    s.test("the page's time: dragged back into a motion, the page is drawn as it was then; played backwards and forwards; live again", [] {
        Site a;
        // The bar: its places' names gather into the other language (the place below the opening is below the fold).
        constexpr std::string_view view = "main/bar", bar = "main/foot/time";
        a.h.advance(seconds(0.5));
        const double switched = a.h.runtime().now().count();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.settle(seconds(3));
        const auto at_rest = a.pixels_of(view);
        check(a.h.tree().node(bar) && a.h.tree().node(bar)->role == Role::slider, "the page's time, a slider");
        check(a.text("main/foot/clock") == "现在", "live");
        // An agent drags the playhead into the switch: the letters gathering, as they were then.
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(switched + 0.15));
        check(a.h.tree().act(bar, "set_value", agent).exit_code() == std::optional<int>(0), "set by an agent, as a person drags it");
        a.h.settle(seconds(2));
        const auto then = a.pixels_of(view);
        check(then != at_rest, "the switch under way, held");
        check(a.text("main/foot/clock").starts_with("−"), "seconds before the newest");
        // Live again: the page as it is.
        check(a.h.click("main/foot/time_live"));
        a.h.settle(seconds(2));
        check(a.pixels_of(view) == at_rest && a.text("main/foot/clock") == "现在", "live: at rest");
        // Backwards: the playhead earlier each frame, until the strip's start.
        check(a.h.click("main/foot/time_back"));
        const double from = a.value(bar);
        for (int i = 0; i < 20; ++i) a.h.step();
        check(a.value(bar) < from, "earlier each frame");
        a.h.advance(seconds(30));
        check(a.value(bar) == a.value(bar, "min"), "stopped at the strip's start");
        // Played forwards from there: at the end, live.
        check(a.h.click("main/foot/time_play"));
        a.h.advance(seconds(30));
        a.h.settle(seconds(2));
        check(a.text("main/foot/clock") == "现在", "live at the end");
        check(a.pixels_of(view) == at_rest, "at rest, as it was");
    });

    s.test("getting started: example 01 runs beside its source, typed into and clicked", [] {
        Site a;
        check(a.h.click("main/bar/nav/to_hello"));
        a.h.settle(seconds(3));
        constexpr std::string_view run = "main/body/view/hello/page";
        const auto field = a.h.tree().find({.under = std::string(run), .role = Role::text_box});
        check(!field.empty(), "example 01's field, found by its role");
        check(!field.empty() && a.h.click(field.front().id), "its field focused");
        a.h.type("Ada");
        a.h.settle();
        const auto button = a.h.tree().find({.under = std::string(run), .role = Role::button});
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        check(!button.empty() && a.h.tree().act(button.front().id, "activate", agent).exit_code() == std::optional<int>(0), "its button activated by an agent");
        a.h.settle();
        bool greeted = false, counted = false;
        for (const auto& n : a.h.tree().snapshot({.root = std::string(run)}).nodes) {
            greeted = greeted || n.name == "Nice to meet you, Ada!";
            counted = counted || n.name == "Clicked once";
        }
        check(greeted, "the program ran: greeted");
        check(counted, "the program ran: counted");
        check(a.text("main/body/view/hello/page/code/source").contains("export module hello;"), "its source beside it");

    });

    s.test("a document's links: another place of the site, or outside through the host", [] {
        Site a;
        Invocation person;  // the person at the machine (not over the bus)
        person.args = Value{}.set("href", Value("features.md"));
        person.answer = "yes";
        check(a.h.tree().act("main/body/view/home/page/doc", "open_link", person).exit_code() == std::optional<int>(0), "a link to the features");
        a.h.settle();
        check(a.shown("features"), "the features shown");
        check(a.h.runtime().opened().empty(), "nothing opened outside");
        check(a.h.click("main/bar/nav/to_home"));
        a.h.settle();
        person.args = Value{}.set("href", Value("https://github.com/openlifa/openmui"));
        check(a.h.tree().act("main/body/view/home/page/doc", "open_link", person).exit_code() == std::optional<int>(0), "the spec's repository");
        check(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openmui"}, "opened by the host");
        check(!a.h.runtime().open_url("javascript:alert(1)"), "never a script");
        // Clicked with the pointer, as a visitor does.
        Site b;
        const auto links = b.h.tree().find({.under = "main/body/view/home/page/doc", .role = Role::link});
        const auto to_features = std::ranges::find_if(links, [](const auto& n) { return n.name.contains("sets openmui apart"); });
        check(to_features != links.end(), "the link to the features");
        if (to_features != links.end()) {
            (void)b.h.tree().act(to_features->id, "scroll_into_view");  // below the opening: brought into view, as a reader scrolls
            b.h.settle(seconds(1));
            check(b.h.click(to_features->id), "clicked");
        }
        b.h.settle(seconds(3));
        check(b.shown("features"), "a click reaches its link");
    });

    s.test("an agent on the page: real requests, openas deciding — it asks first, the person answers, and only the person", [] {
        Site a;
        // In the panel, as a person reaches it: scrolled to, then pressed.
        auto press = [&](const std::string& id) {
            (void)a.h.tree().act(id, "scroll_into_view");
            a.h.settle();
            return a.h.click(id);
        };
        auto run = [&](std::string_view task) {
            // Below the panel's fold, as a person reaches it: scrolled to, then clicked.
            const std::string id = std::format("main/body/guide/tasks/task_{}", task);
            (void)a.h.tree().act(id, "scroll_into_view");
            a.h.settle();
            check(a.h.click(id), std::format("the task {}", task));
            a.h.advance(seconds(4));
            a.h.settle(seconds(1));
        };
        check(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        check(a.visible("main/body/guide"), "the panel");
        const auto said = [&] { return a.text("main/body/guide/said/transcript"); };

        run("read");
        check(said().find(R"(→ {"op":"snapshot","actor":"agent:guide","root":"main/body/view/home/page","depth":2})") != std::string::npos, said());
        check(said().find(R"(← {"kind":"result","exitCode":0})") != std::string::npos, "read: it runs");

        run("tour");
        check(a.text("main/bar/nav/to_features") == "特性", "the language switched by the agent");
        check(a.shown("features"), "and the features open");
        check(a.h.click("main/bar/lang"));
        a.h.settle();

        run("edit");
        check(a.shown("timeline"), "the timeline");
        const Value ed = a.h.tree().node("main/body/view/timeline/page/editor")->value;
        check(ed.get("tracks") && ed.get("tracks")->list()->front().get("clips")->list()->size() == 2, std::format("split, then undone: {}\n{}", ed.json(), said()));
        check(said().find(R"("action":"split")") != std::string::npos && said().find(R"("op":"undo")") != std::string::npos, "split and undo, as requests");

        // External: it asks the person first (exit code 2), and waits.
        run("link");
        check(a.visible("main/body/guide/asking"), "the agent asks");
        check(said().find(R"("exitCode":2)") != std::string::npos, "needs the person's answer");
        check(a.h.runtime().opened().empty(), "nothing opened yet");
        // It cannot answer for the person (S-10), whatever it claims.
        run("self");
        check(said().find("person-only") != std::string::npos || said().find("person's own") != std::string::npos, said());
        check(a.visible("main/body/guide/asking"), "still asking");
        // The person allows it once: the request again with the answer.
        check(press("main/body/guide/asking/answer/allow_once"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        check(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openas"}, "opened, once");
        check(!a.visible("main/body/guide/asking"), "nothing asked now");
        // Once was once; for this visit is a grant.
        run("link");
        check(a.visible("main/body/guide/asking"), "asks again");
        check(press("main/body/guide/asking/answer/allow"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        check(a.h.runtime().opened().size() == 2, "opened");
        check(a.text("main/body/guide/held/grants") == "Grants: agent:guide=external@open_link", a.text("main/body/guide/held/grants"));
        run("link");
        check(!a.visible("main/body/guide/asking") && a.h.runtime().opened().size() == 3, "within the grant: it runs without asking");
        check(press("main/body/guide/held/revoke"));
        a.h.settle();
        check(a.text("main/body/guide/held/grants") == "Grants: none");
        run("link");
        check(a.visible("main/body/guide/asking"), "revoked: it asks again");
        check(press("main/body/guide/asking/answer/deny"));
        a.h.settle();
        check(a.h.runtime().opened().size() == 3 && !a.visible("main/body/guide/asking"), "no");
        agent_access().revoke_all();
    });

    s.test("your own agent: the prompt with its pairing code, copied; its actions in the log, each taken back from there; the self-check it presses", [] {
        Site a;
        check(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        // No host to link one here: the prompt says how instead, and nothing is copied.
        check(a.h.click("main/body/guide/copy_prompt"));
        a.h.settle();
        check(a.text("main/body/guide/prompt").find("omui agent") != std::string::npos && a.h.runtime().clipboard_text().empty(), a.text("main/body/guide/prompt"));
        // As the Web links one: a code, the page's agent.md.
        int ended = 0;
        a.h.runtime().set_agent_linker({[&] {
                                            a.h.runtime().agent_link_changed({"waiting", "K7Q4-42MX", {}, {}, 47110, "https://openmui.dev/agent.md"});
                                            return std::string("K7Q4-42MX");
                                        },
                                        [&] { ++ended; }});
        check(a.h.click("main/body/guide/copy_prompt"));
        a.h.settle();
        const std::string prompt = a.text("main/body/guide/prompt");
        check(prompt.find("K7Q4-42MX") != std::string::npos && prompt.find("https://openmui.dev/agent.md") != std::string::npos && prompt.find("port 47110") != std::string::npos,
              prompt);
        check_eq(a.h.runtime().clipboard_text(), prompt);
        check(a.h.tree().node("main/body/guide/log")->value.get("link")->as_string() == "waiting", "the log says it waits");
        a.h.runtime().agent_link_changed({"paired", "K7Q4-42MX", "claude-code", {}, 47110, "https://openmui.dev/agent.md"});
        a.h.settle();
        // The agent, as agent.md has it: the self-check, then the person's task (a box of the checklist).
        Invocation agent;
        agent.actor = {"agent", "claude-code", {}};
        check(a.h.tree().act("main/body/guide/check/hello", "activate", agent).exit_code() == std::optional<int>(0));
        a.h.settle();
        check(a.text("main/body/guide/check/hellos") == "pressed 1 time", a.text("main/body/guide/check/hellos"));
        const std::string box = "main/body/view/home/page/hero/demo/specimen/items/tests";
        check(a.h.tree().act(box, "toggle", agent).exit_code() == std::optional<int>(0));
        a.h.settle();
        check(a.h.tree().node(box)->states.has(State::checked), "checked by the agent");
        auto items = a.h.runtime().virtual_items(a.h.runtime().find("main/body/guide/log"));
        check(items.size() == 2 && items[0].name == "claude-code activate …/check/hello" && items[1].name.starts_with("claude-code toggle"),
              items.empty() ? std::string("none") : items.back().name);
        // The person takes it back from the log.
        check(std::ranges::any_of(items[1].actions, [](const spec::ActionDesc& d) { return d.name == "undo"; }), "the last its history holds");
        Invocation person;
        check(a.h.tree().act(items[1].id, "undo", person).exit_code() == std::optional<int>(0));
        a.h.settle();
        check(!a.h.tree().node(box)->states.has(State::checked), "taken back");
        // Ending the link is the person's: the agent cannot, the person can.
        Invocation over_the_bus = agent;
        over_the_bus.remote = true;  // as the bridge brings it
        check(a.h.tree().act("main/body/guide/disconnect", "activate", over_the_bus).exit_code() != std::optional<int>(0), "not the agent's to press");
        check(a.h.click("main/body/guide/disconnect"));
        a.h.settle();
        check(ended == 1 && a.text("main/body/guide/prompt").empty(), "ended by the person");
    });

    s.test("the site's pixels pinned on every platform: home, the components, the wireframe, the agent's JSON, the timeline editor, getting started, an agent asking, the settings, Chinese, the page's time held", [] {
        Site a;
        // Fixed spans of time, not "until quiet": how many frames a platform
        // takes to settle differs a little, and the foot draws the page's time.
        const auto dir = scratch_dir("site");
        Hash all;
        // Each picture's own hash said too: a platform that differs shows which.
        auto shot = [&](const std::string& name) {
            const std::uint64_t p = a.pixels();
            all.u64(p);
            std::println("    {} {:016x}", name, p);
            check(a.h.save_png(dir / name).has_value());
        };
        auto go = [&](std::string_view place) {
            check(a.h.click(std::format("main/bar/nav/to_{}", place)));
            a.h.move({4, 796});
            a.h.advance(seconds(2));
        };
        shot("home.png");
        go("components");
        shot("components.png");
        go("home");
        check(a.h.click("main/foot/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("tree.png");
        check(a.h.click("main/foot/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("agent.png");
        check(a.h.click("main/foot/lens_pixels"));
        go("timeline");
        shot("timeline.png");
        go("hello");
        shot("hello.png");
        // The agent asks before something external (S-11), the answer the person's alone (S-10).
        go("home");
        check(a.h.click("main/bar/guide_toggle"));
        check(a.h.click("main/body/guide/tasks/task_link"));
        a.h.advance(seconds(3));
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        shot("agent-asks.png");
        check(a.h.click("main/body/guide/asking/answer/deny"));
        check(a.h.click("main/bar/settings_toggle"));
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        shot("settings.png");
        check(a.h.click("main/bar/settings_toggle"));
        go("home");
        const double switched = a.h.runtime().now().count();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(2));
        shot("zh.png");
        // The page's time dragged back into the switch: the letters gathering again, paused.
        Invocation person;
        person.args = Value{}.set("value", Value(switched + 0.15));
        check(a.h.tree().act("main/foot/time", "set_value", person).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(1));
        shot("time.png");
        std::println("    pixels {}", all.hex());
        check_eq(all.hex(), std::string("bf7e33bc6b3461c1"));
    });

    s.test("the components: each live, its code in four languages; a style pack styles the gallery alone, the tree the same in every pack", [] {
        Site a("components");
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        const std::string page = "main/body/view/components/page/gallery";
        const auto items = a.h.tree().find({.under = page + "/items", .role = Role::button});
        check(items.size() >= 10, std::format("the specimens' buttons: {}", items.size()));
        check(a.text(page + "/items/badge/code/source") == "badge({.count = 120, .id = \"mail\", .tone = BadgeTone::danger})", a.text(page + "/items/badge/code/source"));
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        check(a.h.tree().act(page + "/controls/language/languages/python", "activate", agent).exit_code() == std::optional<int>(0), "Python's code");
        a.h.settle();
        check(a.text(page + "/items/badge/code/source") == "omui.badge(id=\"mail\", count=120, tone=\"danger\")", a.text(page + "/items/badge/code/source"));
        check(a.h.tree().act(page + "/controls/language/languages/lua", "activate", agent).exit_code() == std::optional<int>(0));
        a.h.settle();
        check(a.text(page + "/items/badge/code/source") == "omui.Badge { id = \"mail\", count = 120, tone = \"danger\" }", a.text(page + "/items/badge/code/source"));
        // A pack: the gallery's specimens in it, the site around them as they were, the tree the same.
        auto tree = [&] {
            std::vector<std::string> out;
            for (const auto& n : a.h.tree().snapshot({.root = page + "/items"}).nodes) out.push_back(std::format("{} {} {}", n.id, spec::name(n.role), n.name));
            return out;
        };
        const auto plain = tree();
        const std::uint64_t chrome = a.pixels_in({0, 0, 1280, 56});
        const std::uint64_t shown = a.pixels_of(page + "/items/button/specimen");
        check(a.h.tree().act(page + "/controls/style/packs/neon", "activate", agent).exit_code() == std::optional<int>(0), "the neon pack");
        a.h.advance(seconds(1));
        check(a.pixels_of(page + "/items/button/specimen") != shown, "the specimens in another pack");
        check(a.pixels_in({0, 0, 1280, 56}) == chrome, "the site's bar as it was");
        check(tree() == plain, "only pixels change");
    });

    s.test("the reference run: each sampled frame's pixels hashed as a browser hashes them, pinned (reference/ci.frames.omui.json)", [] {
        site::ReferenceReplay replay;
        std::vector<std::string> got;
        for (std::int32_t f = 0; f < site::reference_frames(); ++f) got.push_back(std::format("{:016x}", replay.hash(f)));
        check(std::format("{:016x}", replay.hash(17)) == got[17], "a frame played to again, from the start: the same");
        check(std::set(got.begin(), got.end()).size() >= 4, "the run moves: its frames differ");
        const auto pinned = site::reference_pinned();
        bool same = pinned.size() == got.size();
        for (std::size_t i = 0; same && i < got.size(); ++i) same = std::format("{:016x}", pinned[i]) == got[i];
        if (!same) {  // the table as this run makes it, to look at and pin
            std::string json = "{\n  \"about\": \"The site's reference run: each sampled frame's pixel hash as CI computed it (site_test writes and checks it; the page compares a browser's own with it).\",\n  \"fps\": 10,\n  \"size\": [1280, 800],\n  \"frames\": [";
            for (std::size_t i = 0; i < got.size(); ++i) json += std::format("{}\n    \"{}\"", i ? "," : "", got[i]);
            json += "\n  ]\n}\n";
            const auto out = scratch_dir("site-reference") / "ci.frames.omui.json";
            std::ofstream(out, std::ios::binary) << json;
            std::println("    the reference run's table: {}", out.string());
        }
        check(same, std::format("{} frames pinned, {} made; first made {}", pinned.size(), got.size(), got.empty() ? "" : got[0]));
        // A few of its moments, to look at: the flight, the switch, the lens fading in.
        const auto dir = scratch_dir("site-reference-frames");
        for (const std::int32_t f : {9, 30, 48}) {
            (void)replay.hash(f);
            if (auto png = replay.png()) std::ofstream(dir / std::format("frame-{:02}.png", f), std::ios::binary).write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
        }
    });

    s.test("the check against CI in the page: the reference run's frame drawn here, its hash beside CI's, the same", [] {
        Site a;
        check(a.h.click("main/foot/ci_check"));
        a.h.advance(seconds(0.5));
        check(a.visible("main/body/check"), "the panel");
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(9.0));
        check(a.h.tree().act("main/body/check/ref_frame", "set_value", agent).exit_code() == std::optional<int>(0), "a frame chosen");
        a.h.advance(seconds(0.5));
        site::ReferenceReplay own;
        const std::string want = std::format("{:016x}", own.hash(9));
        check(a.text("main/body/check/mine").ends_with(want), a.text("main/body/check/mine"));
        check(a.text("main/body/check/ci").ends_with(want) && a.text("main/body/check/verdict").starts_with("✓"), a.text("main/body/check/verdict"));
        (void)a.h.save_png(scratch_dir("site-check") / "panel.png");
    });

    s.test("the static page: every place's HTML, md4c-html's bytes, in a page that hides it once the program runs", [] {
        for (std::string_view lang : {"en", "zh"}) {
            const std::string html = site::static_html(lang);
            check(html.starts_with("<main id=\"omui-static\""), lang);
            const auto skip = html.find("<a class=\"skip\" href=\"#/"), nav = html.find("<nav aria-label=");
            check(skip != std::string::npos && skip < nav, std::format("{}: a skip link before the places", lang));
            for (const site::Place& p : site::places()) check(html.find(std::format("<section id=\"/{}{}\">", lang == "zh" ? "zh/" : "", p.id)) != std::string::npos, std::format("{} {}", lang, p.id));
            check(html.find(md::to_html(md::parse(site::document("features", lang)))) != std::string::npos, "the features as md4c-html writes them");
        }
        const std::string en = site::static_html("en");
        check(en.find("<h1>One interface. Every rendering.</h1>") != std::string::npos, "the thesis, readable without JavaScript");
        // The site's own part pinned (the docs change with the framework; they are held to md4c-html's bytes above).
        auto own = [](const std::string& html) { return html.substr(0, html.find("<section id=\"/", html.find("<section id=\"/") + 1)); };
        std::println("    static en {:016x} zh {:016x}", fnv(own(en)), fnv(own(site::static_html("zh"))));
        check_eq(std::format("{:016x}", fnv(own(en))), std::string("6f7002018b2f62a8"));
        check_eq(std::format("{:016x}", fnv(own(site::static_html("zh")))), std::string("5fd8dfe72a66426b"));

        const auto out = site::inject(page, en, "zh");
        check(out.has_value(), "dist-toy's page");
        const std::string& p = *out;
        check(p.find("<html lang=\"zh-CN\">") != std::string::npos, "the page's language");
        const auto main = p.find("<main id=\"omui-static\""), canvas = p.find("<canvas id=\"canvas\""), hide = p.find("onRuntimeInitialized = function"),
                   loader = p.find("<script src=\"omui-site.js\">");
        check(main < canvas, "the static page before the canvas");
        check(hide != std::string::npos && hide < loader, "hidden by a script before the program's");
        check(p.find("#omui-static{") < p.find("</head>"), "its style in the head");
        check(p.find("@font-face{font-family:'Inter';src:url(assets/4304c4ca3090.ttf)") != std::string::npos, "in the program's own font");
        check(!site::inject("<html><body>nothing</body></html>", en, "en").has_value(), "a page dist-toy did not write");
        check(!site::inject(p, en, "en").has_value(), "a page already pre-rendered");
    });

    s.test("on a phone: the agent's tour — the panel covers the place, the tour puts it away and the place is read", [] {
        auto h = std::make_unique<Headless>(HeadlessOptions{.size = {390, 844}});
        AppSpec spec = site::app("home", "en");
        h->runtime().set_theme(spec.theme);
        spec.setup(h->runtime());
        h->mount("main", spec.view());
        h->settle();
        check(h->click("main/bar/guide_toggle"), "the agent's panel");
        h->settle();
        check(h->click("main/body/guide/tasks/task_tour"), "the tour");
        h->advance(seconds(6));
        h->settle(seconds(1));
        const auto doc = h->tree().node("main/body/view/features/page/read/doc");
        check(doc && !doc->states.has(State::offscreen), "the features read, in view");
        const Rect view = h->runtime().screen_bounds(h->runtime().find("main/body/view"));
        check(view.width == 390, std::format("the view the whole width again ({})", view.width));
        const Rect live = h->runtime().screen_bounds(h->runtime().find("main/foot/time_live"));
        check(live.height < 60 && live.x + live.width <= 390, std::format("the foot's Live on one line, inside ({} high, to {})", live.height, live.x + live.width));
    });

    s.test("a panel opened beside the place: the place is laid out again to the room left", [] {
        Site a;
        // Landmarks a screen reader jumps between: the places' navigation, the place itself.
        check(a.h.tree().node("main/bar/nav") && a.h.tree().node("main/bar/nav")->role == Role::navigation, "the navigation landmark");
        check(a.h.tree().node("main/bar/nav/to_home")->states.has(State::selected) && !a.h.tree().node("main/bar/nav/to_features")->states.has(State::selected),
              "where you are: the current place selected");
        check(a.h.tree().node("main/body/view") && a.h.tree().node("main/body/view")->role == Role::main, "the main landmark");
        const Rect before = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view"));
        check(a.h.click("main/bar/guide_toggle"));
        a.h.advance(seconds(1));
        const Rect view = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view"));
        check(view.width < before.width, std::format("the view narrower: {} → {}", before.width, view.width));
        const Rect page = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/home/page"));
        check(page.x >= view.x - 1 && page.x + page.width <= view.x + view.width + 1,
              std::format("the page within the view: page {} + {}, view {} + {}", page.x, page.width, view.x, view.width));
        // Nothing of the page's content under the panel.
        std::string under;
        for (const auto& n : a.h.tree().snapshot({.root = "main/body/view/home/page"}).nodes)
            if (n.bounds && n.bounds->x + n.bounds->width > view.x + view.width + 1 && !n.states.has(State::offscreen) && !n.states.has(State::hidden))
                under += std::format("{} ({}) ", n.id, n.bounds->x + n.bounds->width);
        check(under.empty(), "nothing under the panel: " + under);
        (void)a.h.save_png(scratch_dir("site-panel") / "home-with-panel.png");
    });


    return s.run(argc, argv);
}
