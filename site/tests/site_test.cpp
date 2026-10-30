// The site as a reader, an agent and a crawler meet it: each place shown in
// turn, in both languages; the lens moving the same page from its pixels to
// the tree's wireframe, the JSON an agent reads and the assertions a test
// writes; the home page's tree hash pinned (what the footer compares in the
// browser); the live timeline editor edited and undone; the static HTML a
// reader without JavaScript gets, written into dist-toy's page.
import std;
import openmui;
import openmui.site;
import openmui.ui.markdown;  // openmui.markdown with it
import openmui.ui.canvas;
import openmui.showcase.fighting;
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
    Value snapshot(std::string_view place) { return h.tree().snapshot({.root = std::format("main/body/view/atlas/{}/page", place)}).to_value(); }
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
    std::uint64_t pixels_in(Rect r) {
        const auto& px = h.pixels();
        const int x0 = std::max(0, static_cast<int>(r.x)), x1 = std::min(px.width(), static_cast<int>(r.x + r.width));
        Hash hash;
        for (int y = std::max(0, static_cast<int>(r.y)); y < std::min(px.height(), static_cast<int>(r.y + r.height)); ++y)
            hash.bytes(std::as_bytes(std::span(px.row(y) + x0, static_cast<std::size_t>(std::max(0, x1 - x0)))));
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
    // A place is shown when its window is live (not a texture on the atlas).
    bool shown(std::string_view place) { return h.tree().node(std::format("main/body/view/atlas/{}/page", place)).has_value(); }
};

std::uint64_t fnv(std::string_view s) {
    std::uint64_t h = 1469598103934665603ull;
    for (const char c : s) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
    return h;
}

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
            if (p.id == "timeline" || p.id == "wall" || p.id == "styles" || p.id == "hello" || p.id == "moves") {
                check(site::document(p.id, "en").empty(), std::format("{} is a live work, not a document", p.id));
                continue;
            }
            for (std::string_view lang : {"en", "zh"}) {
                const std::string_view doc = site::document(p.id, lang);
                check(doc.starts_with("# "), std::format("{} in {}: a document with its title", p.id, lang));
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
            check(a.h.click(std::format("main/body/nav/to_{}", p.id)), std::format("the nav's {}", p.id));
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
        check_eq(a.text("main/body/nav/to_features"), std::string("Features"));
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check_eq(a.text("main/body/nav/to_features"), std::string("特性"));
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

    s.test("the lens: pixels, the tree's wireframe, an agent's JSON, a test's assertions — of the same nodes", [] {
        Site a;
        const auto plain = a.page();
        check(!a.visible("main/body/view/wireframe") && !a.visible("main/body/view/agent"), "only the pixels");
        check(a.h.click("main/bar/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        check(a.visible("main/body/view/wireframe"), "the wireframe over the page");
        check(a.pixels() != plain, "the wireframe drawn");
        const auto wire = a.h.tree().node("main/body/view/wireframe");
        const std::int64_t drawn = wire->value.get("nodes") ? wire->value.get("nodes")->as_int() : 0;
        const auto home = a.snapshot("home");
        check(drawn == static_cast<std::int64_t>(home.get("nodes")->list()->size()), "every node of the place outlined");

        check(a.h.click("main/bar/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string json = a.text("main/body/view/agent/agent_text");
        check(json.find("\"id\":\"main/body/view/atlas/home") != std::string::npos || json.find("\"id\": \"main/body/view/atlas/home") != std::string::npos,
              "the nodes as an agent reads them");
        for (const Value& n : *home.get("nodes")->list()) check(json.find(n.get("id")->json()) != std::string::npos, std::format("{} in the agent's JSON", n.get("id")->json()));

        check(a.h.click("main/bar/lens_test"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string lines = a.text("main/body/view/test/test_text");
        check(lines.find("check_eq(node(\"main/body/view/atlas/home") != std::string::npos, "assertions of the same nodes");
        check(lines.find("check_eq(node(\"main/body/view/atlas/home/page/doc/b0\").role, \"heading\");") != std::string::npos, "the title's role asserted");

        check(a.h.click("main/bar/lens_pixels"));
        a.h.move({4, 796});  // the pointer off the buttons (no hover)
        a.h.settle(seconds(1));
        check(a.page() == plain, "back to the pixels, the same pixels (above the foot, where the lenses are marked)");
    });

    s.test("the home page's tree: pinned, the same in a browser as here; boxes do not count", [] {
        Site a;
        const auto h = site::tree_hash(a.snapshot("home"));
        std::println("    home tree {:016x}", h);
        check(h == site::pinned_home_hash, "the hash the footer compares");
        check(a.text("main/foot/hash").find("✓") != std::string::npos, a.text("main/foot/hash"));
        // A narrower window moves every box, scrolls further and leaves some of the page off screen: not the tree.
        Headless narrow{HeadlessOptions{.size = {820, 640}}};
        AppSpec spec = site::app();
        spec.setup(narrow.runtime());
        narrow.mount("main", spec.view());
        narrow.settle();
        check(site::tree_hash(narrow.tree().snapshot({.root = "main/body/view/atlas/home/page"}).to_value()) == h, "the same tree, another window");
        // The Chinese page is another tree.
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check(site::tree_hash(a.snapshot("home")) != h, "another language, another tree");
    });

    s.test("the live timeline editor: an agent's edit, the person's undo, the tree that follows", [] {
        Site a;
        check(a.h.click("main/body/nav/to_timeline"));
        a.h.settle();
        constexpr std::string_view editor = "main/body/view/atlas/timeline/page/editor";
        const auto before = site::tree_hash(a.snapshot("timeline"));
        auto tracks = [&] { return a.h.tree().node(editor)->value.get("tracks")->json(); };
        const std::string was = tracks();
        Invocation agent;
        agent.actor = {"agent", "test", {}};
        agent.args = Value{}.set("track", 0).set("clip", 1).set("to_track", 0).set("frame", 100);
        check(a.h.tree().act(editor, "move", agent).exit_code().value_or(-1) == std::int64_t{0}, "an agent moves the fight");
        a.h.settle();
        check(site::tree_hash(a.snapshot("timeline")) != before, "the tree follows the edit");
        check(a.text("main/foot/hash") != std::format("tree {:016x}", before), "and so does the footer");
        check(a.h.tree().act(editor, "undo", agent).exit_code().value_or(-1) == std::int64_t{0}, "undone in the editor's own history");
        a.h.settle();
        check(tracks() == was, "the very timeline again");
    });

    s.test("the page's time: dragged back, the page is drawn as it was; played backwards and forwards; live again", [] {
        Site a;
        constexpr std::string_view view = "main/body/view", bar = "main/foot/time";
        const auto at_home = a.pixels_of(view);
        const double before = a.h.runtime().now().count();
        a.h.advance(seconds(0.5));
        check(a.h.click("main/body/nav/to_features"));
        a.h.settle(seconds(4));
        const auto at_features = a.pixels_of(view);
        check(at_features != at_home, "flown to Features");
        check(a.h.tree().node(bar) && a.h.tree().node(bar)->role == Role::slider, "the page's time, a slider");
        check(a.text("main/foot/clock") == "now", "live");

        // An agent drags the playhead to before the flight: the page as it was then.
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(before + 0.25));
        check(a.h.tree().act(bar, "set_value", agent).exit_code() == std::optional<int>(0), "set by an agent, as a person drags it");
        a.h.settle(seconds(2));
        check(a.pixels_of(view) == at_home, "the camera back where it was: Home, at rest, live");
        check(a.text("main/foot/clock").starts_with("−"), "seconds before the newest");
        check(a.h.tree().node("main/body/nav/to_features") && a.value(bar) < a.value(bar, "max"), "the playhead before the strip's end");

        // Live again: the page as it is.
        check(a.h.click("main/foot/time_live"));
        a.h.settle(seconds(2));
        check(a.pixels_of(view) == at_features && a.text("main/foot/clock") == "now", "live: Features, as it is");

        // Backwards: the playhead earlier each frame, the camera flying back, until the strip's start.
        check(a.h.click("main/foot/time_back"));
        const double from = a.value(bar);
        for (int i = 0; i < 20; ++i) a.h.step();
        check(a.value(bar) < from, "earlier each frame");
        a.h.advance(seconds(30));
        check(a.value(bar) == a.value(bar, "min"), "stopped at the strip's start");
        check(a.pixels_of(view) == at_home, "back at Home, where the page began");

        // Played forwards from there: the flight again, and at the end, live.
        check(a.h.click("main/foot/time_play"));
        a.h.advance(seconds(30));
        a.h.settle(seconds(2));
        check(a.text("main/foot/clock") == "now", "live at the end");
        check(a.pixels_of(view) == at_features, "Features, as the flight left it");
    });

    s.test("the works run in their places: example 01 typed into and clicked, S1's wall live with the site's documents", [] {
        Site a;
        check(a.h.click("main/body/nav/to_hello"));
        a.h.settle(seconds(3));
        constexpr std::string_view run = "main/body/view/atlas/hello/page";
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
            counted = counted || n.name == "Clicked 1 times";
        }
        check(greeted, "the program ran: greeted");
        check(counted, "the program ran: counted");
        check(a.text("main/body/view/atlas/hello/page/code/source").contains("export module hello;"), "its source beside it");

        check(a.h.click("main/body/nav/to_wall"));
        a.h.settle(seconds(3));
        const auto wall = a.h.tree().node("main/body/view/atlas/wall/page/wall");
        check(wall && wall->role == Role::canvas, "the wall's canvas, live");
        const auto windows = a.h.tree().find({.under = "main/body/view/atlas/wall/page/wall", .role = Role::window});
        check(windows.size() >= 6, std::format("its windows: {}", windows.size()));
    });

    s.test("the move list: frame data the game measured, a move chosen by an agent, shown on the game's own stage at a frame", [] {
        Site a;
        check(a.h.click("main/body/nav/to_moves"));
        a.h.advance(seconds(3));
        constexpr std::string_view page = "main/body/view/atlas/moves/page";
        const auto& r = fighting::roster();
        const gongfu::Fighter& mo = r.fighters[fighting::fighter_index("mo")];
        // Every row is what gongfu.training measured for it.
        const std::int32_t heavy = mo.find("5HP");
        const gongfu::training::FrameData d = gongfu::training::frame_data(r.system, mo, heavy);
        const std::string row = std::format("{}/list/moves/table/move_5hp", page);
        check(a.h.tree().node(row + "/name/pick").has_value(), "a row for 5HP");
        std::vector<std::string> cells;
        for (const auto& n : a.h.tree().snapshot({.root = row}).nodes)
            if (n.role == Role::text && !n.id.contains("/pick")) cells.push_back(n.name);
        check(cells.size() >= 6 && cells[0] == std::format("{}", d.startup) && cells[1] == std::format("{}", d.active) && cells[2] == std::format("{}", d.recovery),
              cells.empty() ? std::string("no cells") : cells[0]);
        // An agent picks it, on block, and drags the frame to its first active one.
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        check(a.h.tree().act(row + "/name/pick", "activate", agent).exit_code() == std::optional<int>(0), "picked by an agent");
        check(a.h.tree().act(std::format("{}/show/controls/on_block", page), "activate", agent).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.2));
        agent.args = Value{}.set("value", Value(static_cast<double>(d.startup)));
        check(a.h.tree().act(std::format("{}/show/controls/frame", page), "set_value", agent).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.2));
        const auto stage = a.h.tree().node(std::format("{}/show/frame_box/stage", page));
        const Value* demo = stage && stage->value.get("training") ? stage->value.get("training")->get("demo") : nullptr;
        check(demo && demo->get("move")->as_string() == "5HP" && demo->get("guard")->as_bool() && demo->get("frame")->as_int() == d.startup,
              demo ? demo->json() : std::string("no demonstration"));
        (void)a.h.save_png(scratch_dir("site-moves") / "moves.png");
    });

    s.test("a document's links: another place of the site, or outside through the host", [] {
        Site a;
        Invocation person;  // the person at the machine (not over the bus)
        person.args = Value{}.set("href", Value("features.md"));
        person.answer = "yes";
        check(a.h.tree().act("main/body/view/atlas/home/page/doc", "open_link", person).exit_code() == std::optional<int>(0), "a link to the features");
        a.h.settle();
        check(a.shown("features"), "the features shown");
        check(a.h.runtime().opened().empty(), "nothing opened outside");
        check(a.h.click("main/body/nav/to_home"));
        a.h.settle();
        person.args = Value{}.set("href", Value("https://github.com/openlifa/openmui"));
        check(a.h.tree().act("main/body/view/atlas/home/page/doc", "open_link", person).exit_code() == std::optional<int>(0), "the spec's repository");
        check(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openmui"}, "opened by the host");
        check(!a.h.runtime().open_url("javascript:alert(1)"), "never a script");
    });

    s.test("an agent on the page: real requests, openas deciding — it asks first, the person answers, and only the person", [] {
        Site a;
        auto run = [&](std::string_view task) {
            check(a.h.click(std::format("main/body/guide/tasks/task_{}", task)), std::format("the task {}", task));
            a.h.advance(seconds(4));
            a.h.settle(seconds(1));
        };
        check(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        check(a.visible("main/body/guide"), "the panel");
        const auto said = [&] { return a.text("main/body/guide/said/transcript"); };

        run("read");
        check(said().find(R"(→ {"op":"snapshot","actor":"agent:guide","root":"main/body/view/atlas/home/page","depth":2})") != std::string::npos, said());
        check(said().find(R"(← {"kind":"result","exitCode":0})") != std::string::npos, "read: it runs");

        run("tour");
        check(a.text("main/body/nav/to_features") == "特性", "the language switched by the agent");
        check(a.shown("features"), "and the features open");
        check(a.h.click("main/bar/lang"));
        a.h.settle();

        run("edit");
        check(a.shown("timeline"), "the timeline");
        const Value ed = a.h.tree().node("main/body/view/atlas/timeline/page/editor")->value;
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
        check(a.h.click("main/body/guide/asking/answer/allow_once"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        check(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openas"}, "opened, once");
        check(!a.visible("main/body/guide/asking"), "nothing asked now");
        // Once was once; for this visit is a grant.
        run("link");
        check(a.visible("main/body/guide/asking"), "asks again");
        check(a.h.click("main/body/guide/asking/answer/allow"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        check(a.h.runtime().opened().size() == 2, "opened");
        check(a.text("main/body/guide/held/grants") == "Grants: agent:guide=external@open_link", a.text("main/body/guide/held/grants"));
        run("link");
        check(!a.visible("main/body/guide/asking") && a.h.runtime().opened().size() == 3, "within the grant: it runs without asking");
        check(a.h.click("main/body/guide/held/revoke"));
        a.h.settle();
        check(a.text("main/body/guide/held/grants") == "Grants: none");
        run("link");
        check(a.visible("main/body/guide/asking"), "revoked: it asks again");
        check(a.h.click("main/body/guide/asking/answer/deny"));
        a.h.settle();
        check(a.h.runtime().opened().size() == 3 && !a.visible("main/body/guide/asking"), "no");
        agent_access().revoke_all();
    });

    s.test("the site's pixels pinned on every platform: home, the overview, the wireframe, the agent's JSON, the works (the wall, the style workshop, example 01, the move list), an agent asking, Chinese, the page's time dragged back", [] {
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
        shot("home.png");
        // The overview: every place a window on the atlas, the camera flown out.
        check(a.h.click("main/body/nav/to_overview"));
        a.h.move({4, 796});
        a.h.advance(seconds(3));
        shot("overview.png");
        check(a.h.click("main/body/nav/to_home"));
        a.h.move({4, 796});
        a.h.advance(seconds(3));
        check(a.h.click("main/bar/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("tree.png");
        check(a.h.click("main/bar/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("agent.png");
        check(a.h.click("main/bar/lens_pixels"));
        check(a.h.click("main/body/nav/to_timeline"));
        a.h.move({4, 796});
        a.h.advance(seconds(2));
        shot("timeline.png");
        for (std::string_view work : {"wall", "styles", "hello", "moves"}) {
            check(a.h.click(std::format("main/body/nav/to_{}", work)));
            a.h.move({4, 796});
            a.h.advance(seconds(3));
            shot(std::format("{}.png", work));
        }
        // The agent asks before something external (S-11), the answer the person's alone (S-10).
        check(a.h.click("main/body/nav/to_home"));
        check(a.h.click("main/bar/guide_toggle"));
        check(a.h.click("main/body/guide/tasks/task_link"));
        a.h.advance(seconds(3));
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        shot("agent-asks.png");
        check(a.h.click("main/body/guide/asking/answer/deny"));
        check(a.h.click("main/bar/guide_toggle"));
        check(a.h.click("main/body/nav/to_home"));
        a.h.advance(seconds(3));
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
        check_eq(all.hex(), std::string("80289b91e50ce4f2"));
    });

    s.test("the style workshop runs in its place, a theme scope: a pack styles it alone, the site keeps its theme, the tree is the same in every pack", [] {
        Site a;
        check(a.h.click("main/body/nav/to_styles"));
        a.h.move({4, 796});
        a.h.advance(seconds(3));
        const std::string work = "main/body/view/atlas/styles/page/workshop/studio";
        check(a.visible(work + "/preview"), "the workshop, live close up");
        check(!a.h.tree().node(work + "/sidebar/export"), "no Export: a page has nowhere to write");
        // The workshop's tree: what an agent reads of it, the same whatever the pack.
        auto tree = [&] {
            std::vector<std::string> out;
            for (const auto& n : a.h.tree().snapshot({.root = work + "/preview"}).nodes) out.push_back(std::format("{} {} {}", n.id, spec::name(n.role), n.name));
            return out;
        };
        const auto glass = tree();
        const std::uint64_t chrome = a.pixels_in({0, 0, 1280, 56});  // the site's bar, in the site's theme
        const std::uint64_t shown = a.pixels_of(work);
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("name", Value("retro"));
        check(a.h.runtime().verbs().invoke("styles.show", agent).exit_code() == std::optional<std::int64_t>(0), "an agent shows the retro pack");
        a.h.advance(seconds(1));
        check(a.pixels_of(work) != shown, "the workshop in another pack");
        check(a.pixels_in({0, 0, 1280, 56}) == chrome, "the site around it as it was");
        check(a.h.runtime().theme_of(a.h.runtime().find("main/body/view/atlas/styles/page/title")).color("surface") == Theme::dark_theme().color("surface"),
              "the place's own title in the site's theme");
        check(tree() == glass, "only pixels change");
        // A person edits the pack: its corners, at once.
        Invocation person;
        person.args = Value{}.set("value", Value(20.0));
        check(a.h.tree().act(work + "/editor/corners", "set_value", person).exit_code() == std::optional<int>(0), "the corners slider");
        a.h.advance(seconds(0.5));
        check(a.h.runtime().scope_theme("workshop") && a.h.runtime().scope_theme("workshop")->corner("medium", 0) == 20.0f, "the corners edited");
    });

    s.test("the reference run: each sampled frame's pixels hashed as a browser hashes them, pinned (reference/ci.frames.omui.json)", [] {
        site::ReferenceReplay replay;
        std::vector<std::string> got;
        for (std::int32_t f = 0; f < site::reference_frames(); ++f) got.push_back(std::format("{:016x}", replay.hash(f)));
        check(std::format("{:016x}", replay.hash(17)) == got[17], "a frame played to again, from the start: the same");
        check(std::set(got.begin(), got.end()).size() > got.size() / 2, "the run moves: most frames differ");
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
            for (const site::Place& p : site::places()) check(html.find(std::format("<section id=\"/{}{}\">", lang == "zh" ? "zh/" : "", p.id)) != std::string::npos, std::format("{} {}", lang, p.id));
            check(html.find(md::to_html(md::parse(site::document("features", lang)))) != std::string::npos, "the features as md4c-html writes them");
        }
        const std::string en = site::static_html("en");
        check(en.find("<h1>One tree, many renderings</h1>") != std::string::npos, "the thesis, readable without JavaScript");
        {  // The move list without the program: every fighter's frame data, as measured.
            const auto& r = fighting::roster();
            const gongfu::Fighter& mo = r.fighters[fighting::fighter_index("mo")];
            const auto d = gongfu::training::frame_data(r.system, mo, mo.find("5HP"));
            check(en.find("<h2>Mo</h2>") != std::string::npos && site::static_html("zh").find("<h2>墨</h2>") != std::string::npos, "each fighter, in both languages");
            check(en.find(std::format("<tr><td>5HP</td><td>{}</td><td>{}</td><td>{}</td>", d.startup, d.active, d.recovery)) != std::string::npos, "its moves' measured frames");
        }
        // The site's own part pinned (the docs change with the framework; they are held to md4c-html's bytes above).
        auto own = [](const std::string& html) { return html.substr(0, html.find("<section id=\"/", html.find("<section id=\"/") + 1)); };
        std::println("    static en {:016x} zh {:016x}", fnv(own(en)), fnv(own(site::static_html("zh"))));
        check_eq(std::format("{:016x}", fnv(own(en))), std::string("204dbdae45bacdce"));
        check_eq(std::format("{:016x}", fnv(own(site::static_html("zh")))), std::string("a752fb5e438266de"));

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

    return s.run(argc, argv);
}
