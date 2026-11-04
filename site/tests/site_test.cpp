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

const std::filesystem::path& workspace_media() {
    static const auto dir = scratch_dir("site-workspace");
    return dir;
}

struct Site {
    Headless h;
    explicit Site(std::string_view place = "home", std::string_view lang = "en") : h{HeadlessOptions{.size = {1280, 800}}} {
        AppSpec spec = site::app(place, lang);
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle(seconds(0.9));
        // These broad regression tests inspect fixed states. Dedicated
        // landing/gallery tests cover the default automatic previews.
        for (const auto id : {"main/body/view/home/page/landing/scene/controls/pause", "main/body/view/components/page/gallery/detail/button/stage/actions/pause"})
            if (const auto node = h.runtime().find(id); h.runtime().valid(node)) (void)h.runtime().act(node, "activate");
        h.settle(seconds(0.5));
    }
    void inspect_object() { if (!h.tree().node("main/body/view/release/page/hero/demo/renderings")) { check(h.click("main/body/view/release/page/hero/demo/context/inspect")); h.settle(); } }
    void inspect() { if (!h.tree().node("main/foot/lens_tree")) { check(h.click("main/foot/tools_toggle")); h.settle(); } }
    bool navigate(std::string_view place) {
        if (place == "home") return h.click("main/bar/mark");
        if (place == "hello") return h.click("main/bar/start");
        if (const auto* destination = site::find_place(place); destination && destination->section == "core") {
            if (!h.click("main/bar/nav/core/open")) return false;
            h.settle(seconds(0.5));
            return h.click(std::format("main/bar/nav/core/menu/to_{}", place));
        }
        if (place == "features" || place == "practices") {
            if (!h.click("main/bar/nav/to_docs")) return false;
            h.settle();
            return h.click(std::format("main/body/view/docs/page/directory/to_{}", place));
        }
        if (place == "brand" || place == "timeline" || place == "release") {
            if (!h.click("main/bar/nav/to_explore")) return false;
            h.settle();
            const auto open = std::format("main/body/view/explore/page/explore/works/{}/open", place);
            if (h.tree().act(open, "scroll_into_view").exit_code() != std::optional<int>(0)) return false;
            h.settle(seconds(0.5));
            return h.click(open);
        }
        return h.click(std::format("main/bar/nav/to_{}", place));
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
    s.test("only documentation has Markdown, in both languages; the home and programs have their own views", [] {
        for (const site::Place& p : site::places()) {
            if (!site::is_document(p.id) || p.id == "docs") {
                check(site::document(p.id, "en").empty(), std::format("{} is a live page, not a document", p.id));
                continue;
            }
            for (std::string_view lang : {"en", "zh"}) {
                const std::string_view doc = site::document(p.id, lang);
                // Home's title is the opening's claim (hero.cpp); its document begins a level down.
                check(doc.starts_with(p.id == "hello" ? "## " : "# "), std::format("{} in {}: a document with its title", p.id, lang));
                check(!md::parse(doc).root.children.empty(), std::format("{} in {}: reads as Markdown", p.id, lang));
            }
        }
        check(site::document("hello", "zh").contains("mcpp"), "the Chinese quickstart");
        check(site::document("nowhere", "en").empty());
    });

    s.test("each place shown in turn: one at a time, its tree under the view", [] {
        Site a;
        check(a.shown("home"), "home first");
        check(!a.shown("features"), "one place at a time");
        for (const site::Place& p : site::places()) {
            if (!p.navigation) continue;  // the mark is reached through the brand link (brand_test)
            check(a.navigate(p.id), std::format("the nav's {}", p.id));
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
        check_eq(a.text("main/bar/nav/to_docs"), std::string("Docs"));
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check_eq(a.text("main/bar/nav/to_docs"), std::string("文档"));
        check_eq(a.text("main/bar/lang"), std::string("English"));
        const std::string doc = a.snapshot("home").json();
        check(doc.find("一个界面，不止一种可能。") != std::string::npos, "the Chinese home page");
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
        // Pause is an actual control and its handler focuses that control.
        // Clicking the language button then moves focus out of the page;
        // the directly opened Chinese fixture must start without that extra
        // focused state when we compare their language semantics.
        std::println("    language fixture focus: switched {}, direct {}",
                     a.h.runtime().path(a.h.runtime().focused()), zh.h.runtime().path(zh.h.runtime().focused()));
        a.h.runtime().focus({}); zh.h.runtime().focus({});
        zh.h.move({4, 796});
        zh.h.settle();
        const auto direct = zh.page();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(0.12));
        const auto moving = a.page();
        check(moving != direct, "under way: the letters still gathering");
        const auto switched_tree = a.snapshot("home"), direct_tree = zh.snapshot("home");
        if (site::tree_hash(switched_tree) != site::tree_hash(direct_tree)) {
            const auto dir = scratch_dir("site-language-tree");
            std::ofstream(dir / "switched.json") << switched_tree.json();
            std::ofstream(dir / "direct.json") << direct_tree.json();
            std::println("    language semantic snapshots: {}; focus switched {}, direct {}", dir.string(),
                         a.h.runtime().path(a.h.runtime().focused()), zh.h.runtime().path(zh.h.runtime().focused()));
        }
        check(site::tree_hash(switched_tree) == site::tree_hash(direct_tree), "the tree is already the other language's: the motion is only paint");
        a.h.settle(seconds(3));
        (void)a.h.save_png(scratch_dir("site-sw") / "a.png");
        check(a.page() == direct, "at rest: the very pixels of the page opened in Chinese (above the foot, where the switch is marked)");
        check(site::tree_hash(a.snapshot("home")) == site::tree_hash(direct_tree), "the semantic tree also remains the directly opened Chinese tree at rest");
    });

    s.test("the release scene: a checklist as a person sees it, and its node as a screen reader says it, an agent sends it, a test asserts it", [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero";
        check(!a.h.tree().node(hero + "/demo/renderings"), "technical views start closed"); a.inspect_object();
        const auto ship = a.h.tree().node(hero + "/demo/specimen/actions/ship");
        check(ship.has_value() && ship->role == Role::button && ship->states.has(State::disabled), "Ship 0.1 waits for its checks");
        const std::string panel = hero + "/demo/renderings/";
        check_eq(a.text(panel + "reader/body/line"), std::string("\"Record release\", button, dimmed"));
        check(a.text(panel + "agent/body/line").find("\"act\": \"specimen/actions/ship\", \"action\": \"activate\"") != std::string::npos, a.text(panel + "agent/body/line"));
        check(a.text(panel + "test/body/line").find("check(node(\"specimen/actions/ship\").states.has(\"disabled\"));") != std::string::npos, a.text(panel + "test/body/line"));
        // A person checks one: the panels follow it, read again from the tree.
        check(a.h.click(hero + "/demo/specimen/items/docs/done"));
        a.h.advance(seconds(0.5));
        check_eq(a.text(panel + "reader/body/line"), std::string("\"Docs reviewed\", checkbox, checked"));
        check(a.text(panel + "test/body/line").find("check(node(\"specimen/items/docs/done\").states.has(\"checked\"));") != std::string::npos,
              a.text(panel + "test/body/line"));
    });

    s.test("the release workspace: add, rename, remove, undo, reset and share the same data with the motion scene", [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen", shape = "main/body/view/release/page/motion/shown/shape";
        Invocation person;
        auto act = [&](std::string id, std::string_view action, Value args = {}) { person.args = std::move(args); check(a.h.tree().act(id, action, person).exit_code() == std::optional<int>(0), id + " " + std::string(action)); a.h.settle(); };
        act(board + "/add/submit", "activate");
        check(a.text(board + "/validation").contains("name"), "empty names get a visible error");
        act(board + "/add/title", "set_value", Value{}.set("value", "  Check the keyboard journey  "));
        act(board + "/add/submit", "activate");
        check_eq(a.text(board + "/items/task-1/done"), std::string("Check the keyboard journey"));
        check_eq(a.text(shape + "/task-1/title"), std::string("Check the keyboard journey"));
        check(a.h.tree().node(board + "/items/task-1/done")->origin == spec::Origin::user_content);
        check(a.h.tree().node(shape + "/task-1/title")->origin == spec::Origin::user_content);
        act(board + "/add/title", "set_value", Value{}.set("value", "A task entered with the keyboard"));
        a.h.runtime().focus(a.h.runtime().find(board + "/add/title"));
        a.h.key(Key::enter); a.h.settle();
        check(a.h.tree().node(board + "/items/task-2/done").has_value(), "Enter submits the task");
        act(board + "/history/undo", "activate");
        check(!a.h.tree().node(board + "/items/task-2/done"), "keyboard submission joins the same undo history");
        act(board + "/items/task-1/done", "toggle");
        check_eq(a.text(shape + "/task-1/state"), std::string("Complete"));
        act(board + "/items/task-1/edit", "activate");
        check(a.h.runtime().path(a.h.runtime().focused()) == board + "/editor/title", "editing focuses the real title field");
        act(board + "/editor/title", "set_value", Value{}.set("value", "Review the keyboard and focus paths"));
        act(board + "/editor/actions/save", "activate");
        check_eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths"));
        check(a.h.runtime().path(a.h.runtime().focused()) == board + "/items/task-1/edit", "save restores focus to the task");
        act(board + "/history/undo", "activate");
        check_eq(a.text(board + "/items/task-1/done"), std::string("Check the keyboard journey"));
        act(board + "/items/task-1/edit", "activate");
        act(board + "/editor/actions/cancel", "activate");
        act(board + "/history/redo", "activate");
        check_eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths"));
        act(board + "/items/task-1/edit", "activate");
        act(board + "/editor/actions/remove", "activate");
        check(!a.h.tree().node(board + "/items/task-1/done"));
        check(!a.h.tree().node(shape + "/task-1/title"));
        act(board + "/history/undo", "activate");
        check(a.h.tree().node(board + "/items/task-1/done")->states.has(State::checked));
        act(board + "/actions/reset", "activate");
        check(!a.h.tree().node(board + "/items/task-1/done"));
        act(board + "/history/undo", "activate");
        check_eq(a.text(board + "/items/task-1/done"), std::string("Review the keyboard and focus paths"));
        check(a.navigate("docs")); a.h.settle(); check(a.navigate("release")); a.h.settle();
        check_eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths"));
        check(a.h.click("main/bar/lang")); a.h.advance(seconds(1));
        check_eq(a.text(board + "/items/task-1/done"), std::string("Review the keyboard and focus paths"));
        check_eq(a.text(board + "/items/docs/done"), std::string("文档已审阅"));
        check_eq(a.text(shape + "/task-1/state"), std::string("已完成"));
        act(board + "/history/copy", "activate");
        check(a.h.runtime().clipboard_text().starts_with("# 发布清单\n"));
        check(a.h.runtime().clipboard_text().contains("- [x] Review the keyboard and focus paths\n"));
        check_eq(a.text(board + "/history/copy"), std::string("清单已复制"));
        check(a.h.save_png(workspace_media() / "edited-zh.png").has_value());
    });

    s.test("workspace layouts: five widths, both languages, editable long names and optional technical views", [] {
        const auto dir = workspace_media();
        std::map<std::string, std::uint64_t, std::less<>> baseline;
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (std::string_view lang : {"en", "zh"}) {
            Headless h{HeadlessOptions{.size = {width, 844}}}; auto spec = site::app("release", lang);
            h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
            const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen";
            auto inside = [&] {
                for (const auto& n : h.tree().snapshot({.root = hero}).nodes) if ((n.role == Role::button || n.role == Role::text_box || n.role == Role::checkbox) && n.bounds && !n.states.has(State::hidden) && !n.states.has(State::offscreen))
                    check(n.bounds->x >= -0.5f && n.bounds->right() <= width + 0.5f, std::format("{} {} at {}: {} + {}", lang, n.id, width, n.bounds->x, n.bounds->width));
            };
            inside();
            const auto hash = site::tree_hash(h.tree().snapshot({.root = "main/body/view/release/page"}).to_value());
            const auto [same, added] = baseline.try_emplace(std::string(lang), hash);
            check(added || same->second == hash, std::format("{} at {} keeps the same semantic tree: {:016x} / {:016x}", lang, width, same->second, hash));
            const bool capture = (width == 390 && lang == "zh") || (width == 1280 && lang == "en");
            if (capture) check(h.save_png(dir / std::format("release-{}-{}.png", lang, static_cast<int>(width))).has_value());
            Invocation input; input.args = Value{}.set("value", lang == "zh" ? "和设计与无障碍团队一起检查这一轮发布的键盘导航和焦点恢复" : "Review this release with the design and accessibility teams, including keyboard navigation");
            check(h.tree().act(board + "/add/title", "set_value", input).exit_code() == std::optional<int>(0)); h.settle();
            check(h.tree().act(board + "/add/submit", "activate").exit_code() == std::optional<int>(0)); h.settle();
            check(h.tree().act(board + "/items/task-1/done", "scroll_into_view").exit_code() == std::optional<int>(0)); h.settle(); inside();
            check(h.tree().act(board + "/items/task-1/edit", "activate").exit_code() == std::optional<int>(0)); h.settle(); inside();
            check(h.tree().act(hero + "/demo/context/inspect", "activate").exit_code() == std::optional<int>(0)); h.settle(); inside();
            if (capture) check(h.save_png(dir / std::format("technical-{}-{}.png", lang, static_cast<int>(width))).has_value());
        }
    });

    s.test("the task view changes layouts with stable ids, can be interrupted and works with motion off", [] {
        Site a("release"); const std::string scene = "main/body/view/release/page/motion/shown", shape = scene + "/shape";
        auto act = [&](std::string id, std::string_view action) { check(a.h.tree().act(id, action).exit_code() == std::optional<int>(0), id); a.h.settle(); };
        act(scene + "/controls/change", "scroll_into_view"); a.h.advance(seconds(0.3));
        act(scene + "/controls/follow", "toggle");
        if (a.text(scene + "/controls/change") == "Show list") act(scene + "/controls/change", "activate");
        a.h.advance(seconds(1));
        act(shape, "scroll_into_view"); a.h.advance(seconds(0.2));
        const auto rows = a.h.tree().snapshot({.root = shape}).nodes;
        const auto wide = a.h.runtime().bounds(a.h.runtime().find(shape + "/tests"));
        const auto list_pixels = a.pixels_in(a.h.runtime().screen_bounds(a.h.runtime().find(shape)));
        check(a.h.save_png(workspace_media() / "motion-list.png").has_value());
        const auto list_page = a.page();
        check(a.h.tree().act(scene + "/controls/change", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.1));
        const auto moving_page = a.page();
        check(a.h.step().animating, "the interruption is issued while the transition is active");
        check(a.h.save_png(workspace_media() / "motion-between.png").has_value());
        check(a.h.tree().act(scene + "/controls/change", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(1));
        check(std::abs(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width - wide.width) < 1, "an interrupted change returns to the list");
        act(scene + "/controls/change", "activate"); a.h.advance(seconds(1));
        const auto cards = a.h.tree().snapshot({.root = shape}).nodes;
        std::vector<std::string> before, after;
        for (const auto& n : rows) before.push_back(n.id); for (const auto& n : cards) after.push_back(n.id);
        check(before == after, "task ids and descendants remain stable across the two layouts");
        check(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width < wide.width, "the cards use multiple columns");
        check(a.pixels_in(a.h.runtime().screen_bounds(a.h.runtime().find(shape))) != list_pixels, "the real task scene changes its layout");
        check(moving_page != list_page && moving_page != a.page(), "the sampled middle frame differs from both endpoint drawings");
        check(a.h.save_png(workspace_media() / "motion-cards.png").has_value());
        a.h.runtime().set_motion_level(MotionLevel::off);
        act(scene + "/controls/change", "activate"); a.h.advance(seconds(0.1));
        check(std::abs(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width - wide.width) < 1, "motion off still reaches the chosen layout");
        a.h.runtime().set_motion_level(MotionLevel::full);
        act(scene + "/controls/follow", "toggle"); a.h.advance(seconds(0.5));
        check(a.h.runtime().motion_time(a.h.runtime().find(shape)).has_value(), "scroll seeking restarts after manual mode");
        check(a.navigate("docs")); a.h.settle(); check(a.navigate("release")); a.h.settle(); a.h.advance(seconds(0.5));
        check(a.h.runtime().motion_time(a.h.runtime().find(shape)).has_value(), "restored scroll binds the new scene after a page visit");
    });

    s.test("the simulated agent uses edited task ids, and leaving the page cancels its pending run", [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen";
        Invocation person; person.args = Value{}.set("value", "Check the new task");
        check(a.h.tree().act(board + "/add/title", "set_value", person).exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.tree().act(board + "/add/submit", "activate").exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(7));
        check(a.h.tree().node(board + "/items/task-1/done")->states.has(State::checked));
        check(a.text(board + "/shipped").starts_with("Shipped"));
        check(a.h.tree().act(board + "/actions/reset", "activate").exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.2));
        check(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0)); a.h.advance(seconds(1));
        check(!a.h.tree().node(board + "/items/tests/done")->states.has(State::checked), "stopping cancels the pending scripted step");
        check(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        check(a.navigate("docs")); a.h.advance(seconds(6));
        check(a.navigate("release")); a.h.settle();
        check(!a.h.tree().node(board + "/items/tests/done")->states.has(State::checked), "cancelled timer does not edit after navigation");
        check(!a.h.tree().node(hero + "/agent_row/let_agent")->states.has(State::disabled));
    });

    s.test("the release scene's agent ships it: each step a real action as an agent, through the same checks, each one undoable", [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero";
        a.inspect_object();
        check(a.h.tree().act(hero + "/agent_row/let_agent", "scroll_into_view").exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.click(hero + "/agent_row/let_agent"), "a person asks the agent");
        a.h.advance(seconds(6));
        for (const char* item : {"tests", "docs", "notes"}) {
            const auto n = a.h.tree().node(hero + "/demo/specimen/items/" + item + "/done");
            check(n && n->states.has(State::checked), std::format("{} checked by the agent", item));
        }
        check(a.text(hero + "/demo/specimen/shipped").starts_with("Shipped"), "shipped");
        check(a.text(hero + "/demo/renderings/agent/body/line").find("done") != std::string::npos, a.text(hero + "/demo/renderings/agent/body/line"));
        const auto agents = std::ranges::count_if(a.h.runtime().action_journal(), [](const ActionEntry& e) { return e.actor.starts_with("agent"); });
        check(agents >= 4, std::format("{} actions journaled as the agent's", agents));
        // Every step can be taken back, the shipping too: undo unships it, the panels with it; redo ships it again.
        const auto shipped = [&] { return a.text(hero + "/demo/specimen/shipped").starts_with("Shipped"); };
        check_eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(!shipped(), "undone: not shipped");
        check(a.text(hero + "/demo/renderings/agent/body/line").find("done") == std::string::npos, "the agent's request is past");
        const auto items_checked = [&] {
            int n = 0;
            for (const char* item : {"tests", "docs", "notes"})
                if (auto x = a.h.tree().node(hero + "/demo/specimen/items/" + item + "/done"); x && x->states.has(State::checked)) ++n;
            return n;
        };
        check_eq(items_checked(), 3);  // the checks stay: the shipping alone was taken back
        check_eq(a.h.runtime().redo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(shipped(), "redone: shipped");
        check_eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0});
        check_eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0});
        a.h.advance(seconds(0.3));
        check(!shipped() && items_checked() == 2, "then the last check");
    });

    s.test("on a phone: one header line, a compact menu, the full-width place and a panel in its place", [] {
        Headless h{HeadlessOptions{.size = {390, 844}}};
        AppSpec spec = site::app("release", "zh");
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.settle();
        const auto shown = [&](std::string_view id) {
            const auto n = h.tree().node(id);
            return n && !n->states.has(State::hidden) && !n->states.has(State::offscreen);
        };
        check(shown("main/bar/menu_toggle") && shown("main/foot/tools_toggle"), "compact navigation and opt-in inspection");
        check(!h.tree().node("main/foot/lens_tree"), "no inspector controls on the reading surface");
        const auto brand = h.runtime().screen_bounds(h.runtime().find("main/bar/mark"));
        const auto language = h.runtime().screen_bounds(h.runtime().find("main/bar/lang"));
        check(std::abs(brand.y - language.y) < 1, "language stays on the header line");
        check(h.click("main/bar/menu_toggle")); h.settle();
        check(shown("main/bar/nav/to_docs"), "menu exposes the primary destinations");
        const Rect nav = h.runtime().screen_bounds(h.runtime().find("main/bar/nav"));
        check(nav.x >= 0 && nav.x + nav.width <= 390.5f, std::format("the places within the phone: {} + {}", nav.x, nav.width));
        const Rect view = h.runtime().screen_bounds(h.runtime().find("main/body/view"));
        check(view.width == 390, std::format("the view the whole width: {}", view.width));
        check(h.tree().act("main/body/view/release/page/hero/demo/context/inspect", "activate").exit_code() == std::optional<int>(0)); h.settle();
        // The renderings' words whole: no line of a card below its card's bottom.
        for (const char* card : {"reader", "agent", "test"}) {
            const std::string base = std::format("main/body/view/release/page/hero/demo/renderings/{}", card);
            check(h.runtime().valid(h.runtime().find(base + "/body/line")), base);
            const Rect c = h.runtime().screen_bounds(h.runtime().find(base));
            const Rect l = h.runtime().screen_bounds(h.runtime().find(base + "/body/line"));
            check(l.y + l.height <= c.y + c.height + 0.5f, std::format("{}'s line inside its card: {} vs {}", card, l.y + l.height, c.y + c.height));
        }
        for (const char* id : {"hero/demo/specimen/actions/ship", "hero/agent_row/let_agent", "hero/cta/to_explore"}) {
            const Rect r = h.runtime().screen_bounds(h.runtime().find(std::format("main/body/view/release/page/{}", id)));
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
        check(h.click("main/bar/menu_toggle")); h.settle();
        check(h.click("main/bar/nav/to_docs")); h.settle();
        check(h.click("main/body/view/docs/page/directory/to_features")); h.settle();
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
        a.inspect();
        const auto plain = a.page();
        check(!a.visible("main/body/view/wireframe") && !a.visible("main/body/view/lens_agent"), "only the pixels");
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
        const std::string json = a.text("main/body/view/lens_agent/agent_text");
        check(json.starts_with("// openmui.tree@0.1's snapshot of main/body/view/home"), "the place named once");
        check(json.find("\"id\":\"…/page/landing/intro/title\"") != std::string::npos, "the nodes as an agent reads them, from the place");
        for (const Value& n : *home.get("nodes")->list()) {
            std::string id = std::string(n.get("id")->as_string());
            if (id.starts_with("main/body/view/home/")) id = "…/" + id.substr(std::string_view("main/body/view/home/").size());
            check(json.find(Value(id).json()) != std::string::npos, std::format("{} in the agent's JSON", id));
        }

        check(a.h.click("main/foot/lens_test"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string lines = a.text("main/body/view/lens_test/test_text");
        check(lines.find("tree.node(std::format(\"main/body/view/home/{}\", path))") != std::string::npos, "the place's nodes found under it");
        check(lines.find("check_eq(node(\"page/landing/intro/title\").role, \"heading\");") != std::string::npos, "the title's role asserted");

        check(a.h.click("main/foot/lens_pixels"));
        a.h.move({4, 796});  // the pointer off the buttons (no hover)
        a.h.settle(seconds(1));
        check(a.page() == plain, "back to the pixels, the same pixels (above the foot, where the lenses are marked)");
    });

    s.test("the timeline's monitor: what plays at the playhead, the title fading as its clip does; Play runs the playhead and stops at the end", [] {
        Site a;
        check(a.navigate("timeline"));
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

    s.test("the home is a short landing with one shared mark and two clear destinations", [] {
        Site a;
        const std::string landing = "main/body/view/home/page/landing";
        const auto title = a.h.tree().node(landing + "/intro/title");
        check(title && title->role == Role::heading && title->name == "One interface. Every perspective.");
        for (const auto section : {"hero", "agent", "time", "motion", "fast", "doc"})
            check(!a.h.tree().node(std::string("main/body/view/home/page/") + section), "the full workspaces live on their own pages");
        for (const auto part : {"outline", "letter_m", "letter_u", "letter_i"})
            check(a.h.tree().node(landing + "/scene/visual/mark/" + part + "/path").has_value(), part);
        check(a.h.click(landing + "/intro/actions/explore")); a.h.settle();
        check(a.shown("components"));
        check(a.navigate("home")); a.h.settle();
        check(a.h.click(landing + "/intro/actions/creative")); a.h.settle();
        check(a.shown("explore"));
    });

    s.test("the release scene's Get started action opens the quickstart and real program", [] {
        Site a("release");
        check(!a.h.tree().node("main/body/view/release/page/doc"), "the live scene has no long document");
        check(a.h.click("main/body/view/release/page/hero/cta/to_start"));
        a.h.settle(seconds(2));
        check(a.shown("hello"));
        const std::string intro = "main/body/view/hello/page/example/intro";
        check(a.h.tree().act("main/body/view/hello/page/instructions/copy_commands", "activate").exit_code() == std::optional<int>(0));
        check_eq(a.h.runtime().clipboard_text(), std::string(site::quickstart_commands));
        check(!a.h.tree().find({.under = intro + "/run", .role = Role::text_box}).empty(), "a usable program beside its source");
    });

    s.test("the home page's tree: pinned, the same in a browser as here; boxes do not count", [] {
        Site a;
        a.inspect();
        const auto h = site::tree_hash(a.snapshot("home"));
        std::println("    home tree {:016x}", h);
        check(h == site::pinned_home_hash, "the controlled home baseline is pinned");
        check(a.text("main/foot/proof/hash") == std::format("tree {:08x}", h >> 32), "the footer describes the visitor's current tree");
        Value changed_boxes = a.snapshot("home");
        Value::List moved = *changed_boxes.get("nodes")->list();
        for (auto& node : moved) node.set("bounds", Value::List{Value(100), Value(200), Value(30), Value(40)});
        changed_boxes.set("nodes", Value(std::move(moved)));
        check(site::tree_hash(changed_boxes) == h, "geometry does not change the semantic hash");
        // The compact landing places the CTAs after the visual. The same
        // heading and actions remain available through that responsive layout.
        Headless narrow{HeadlessOptions{.size = {820, 640}}};
        AppSpec spec = site::app();
        narrow.runtime().set_theme(spec.theme);
        spec.setup(narrow.runtime());
        narrow.mount("main", spec.view());
        narrow.settle(seconds(0.9));
        check(narrow.tree().node("main/body/view/home/page/landing/intro/title")->name == "One interface. Every perspective.");
        for (const auto name : {"Explore components", "Creative demos"}) {
            const auto buttons = narrow.tree().find({.under = "main/body/view/home/page/landing", .role = Role::button});
            check(std::ranges::any_of(buttons, [name](const auto& node) { return node.name == name; }), name);
        }
        // The Chinese page is another tree.
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check(site::tree_hash(a.snapshot("home")) != h, "another language, another tree");
    });

    s.test("the live timeline editor: an agent's edit, the person's undo, the tree that follows", [] {
        Site a;
        a.inspect();
        check(a.navigate("timeline"));
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
        Site a("features");
        a.inspect();
        // Documents retain their reading-order line arrival. Navigation uses
        // short native feedback, so it is not this long-motion replay fixture.
        constexpr std::string_view view = "main/body/view/features/page/read/doc", bar = "main/foot/time";
        auto document_pixels = [&] {
            const auto& rt = a.h.runtime();
            Rect area = rt.screen_bounds(rt.find(view)).intersection(rt.screen_bounds(rt.find("main/body/view/features")));
            // Keep the sampled opening inside the document's viewport. Its
            // full text extends below the fold; the time controls do not.
            area.height = std::min(area.height, 320.0f);
            check(area.width > 0 && area.height > 0, "the real document is visible");
            return a.pixels_in(area);
        };
        a.h.advance(seconds(0.5));
        const double switched = a.h.runtime().now().count();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.settle(seconds(3));
        const auto at_rest = document_pixels();
        check(a.h.tree().node(bar) && a.h.tree().node(bar)->role == Role::slider, "the page's time, a slider");
        check(a.text("main/foot/clock") == "现在", "live");
        // An agent drags the playhead into the document's actual line arrival.
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(switched + 0.15));
        check(a.h.tree().act(bar, "set_value", agent).exit_code() == std::optional<int>(0), "set by an agent, as a person drags it");
        a.h.settle(seconds(2));
        const auto then = document_pixels();
        check(then != at_rest, std::format("the document's lines arriving, held: {}", a.text("main/foot/clock")));
        check(a.text("main/foot/clock").starts_with("−"), std::format("seconds before the newest: {} (value {}, target {})", a.text("main/foot/clock"), a.value(bar), switched + 0.15));
        // Live again: the page as it is.
        check(a.h.click("main/foot/time_live"));
        a.h.settle(seconds(2));
        check(document_pixels() == at_rest && a.text("main/foot/clock") == "现在", "live: at rest");
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
        check(document_pixels() == at_rest, "at rest, as it was");
    });

    s.test("getting started: example 01 runs beside its source, typed into and clicked", [] {
        Site a;
        check(a.navigate("hello"));
        a.h.settle(seconds(3));
        constexpr std::string_view run = "main/body/view/hello/page/example/intro/run";
        const auto field = a.h.tree().find({.under = std::string(run), .role = Role::text_box});
        check(!field.empty(), "example 01's field, found by its role");
        if (!field.empty()) { (void)a.h.tree().act(field.front().id, "scroll_into_view"); a.h.settle(); }
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
        check(a.text("main/body/view/hello/page/example/code/source").contains("export module hello;"), "its source beside it");

    });

    s.test("a document's links: another place of the site, or outside through the host", [] {
        Site a("hello");
        const std::string doc = "main/body/view/hello/page/instructions/start";
        Invocation person;
        person.args = Value{}.set("href", Value("features.md")); person.answer = "yes";
        check(a.h.tree().act(doc, "open_link", person).exit_code() == std::optional<int>(0));
        a.h.settle(); check(a.shown("features")); check(a.h.runtime().opened().empty());
        check(a.navigate("docs")); a.h.settle();
        person.args = Value{}.set("href", Value("https://github.com/openlifa/openmui"));
        check(a.h.tree().act("main/body/view/docs/page/read/resources", "open_link", person).exit_code() == std::optional<int>(0));
        check(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openmui"});
        check(!a.h.runtime().open_url("javascript:alert(1)"));
        Site b("hello");
        const auto links = b.h.tree().find({.under = doc, .role = Role::link});
        const auto core = std::ranges::find_if(links, [](const auto& n) { return n.name == "core ideas"; });
        check(core != links.end(), "the actual guide link");
        if (core != links.end()) {
            (void)b.h.tree().act(core->id, "scroll_into_view"); b.h.settle();
            check(b.h.click(core->id));
        }
        b.h.settle(); check(b.shown("features"));
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
        check(a.text("main/bar/nav/to_docs") == "文档", "the language switched by the agent");
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

    s.test("the release scene's screens: your agent, time, motion, speed — the agent panel from its button; the scroll the motion's time; the tier chosen the settings' own", [] {
        Site a("release");
        const std::string home = "main/body/view/release/page/";
        for (const char* id : {"agent", "time", "motion", "fast"}) check(a.h.tree().node(home + id + "/claim").has_value(), std::format("the {} screen", id));
        // The agent screen: the panel, a prompt in it.
        check(a.h.tree().act(home + "agent/shown/actions/copy", "activate").exit_code() == std::optional<int>(0));
        a.h.settle();
        check(a.visible("main/body/guide") && !a.text("main/body/guide/prompt").empty(), "the panel open, a prompt given");
        check(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        // The motion screen: scrolled to, its morph held to the scroll.
        const NodeId shape = a.h.runtime().find(home + "motion/shown/shape");
        Invocation person;
        person.args = Value{}.set("x", 0.0).set("y", 1300.0);
        check(a.h.tree().act("main/body/view/release", "scroll_to", person).exit_code() == std::optional<int>(0));
        a.h.settle();
        a.h.advance(seconds(0.5));  // the hold takes in the frames after the morph starts
        auto at = a.h.runtime().motion_time(shape);
        check(at.has_value(), "its morph held");
        const double part = at ? at->second / at->first : 0;
        person.args = Value{}.set("x", 0.0).set("y", 1700.0);
        (void)a.h.tree().act("main/body/view/release", "scroll_to", person);
        a.h.settle();
        at = a.h.runtime().motion_time(shape);
        check(at && at->second / at->first > part, std::format("further down, further on: {} then {}", part, at ? at->second / at->first : -1));
        // The fast screen: the tier the settings choose.
        check(a.h.tree().act(home + "fast/shown/tier/t1", "activate").exit_code() == std::optional<int>(0));
        a.h.settle();
        check(a.h.runtime().adaptation().effects == pipeline::Tier::t1, "effects as T1");
        check(a.h.tree().node(home + "fast/shown/device")->name.find("T1") != std::string::npos, a.h.tree().node(home + "fast/shown/device")->name);
    });

    s.test("your own agent: the prompt with its pairing code, copied; its actions in the log, each taken back from there; the self-check it presses", [] {
        Site a("release");
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
        const std::string box = "main/body/view/release/page/hero/demo/specimen/items/tests/done";
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
            check(a.navigate(place));
            a.h.move({4, 796});
            a.h.advance(seconds(2));
        };
        shot("home.png");
        go("components");
        shot("components.png");
        go("home");
        a.inspect();
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
        // Reviewed production frames after the text/contrast, sampled-layout
        // and nested default-motion corrections. Intermediate ink, bounds,
        // real input and seek regressions have separate behavioural CTS.
        check_eq(all.hex(), std::string("3027fb6720e6078d"));
    });

    s.test("the components: curated search, live detail, reset, languages and scoped appearance", [] {
        Site a("components");
        a.h.move({4, 796}); a.h.advance(seconds(1));
        const std::string root = "main/body/view/components/page/gallery";
        const std::string overview = root + "/overview";
        check_eq(a.text(overview + "/results/count"), std::string("8 / 36"));
        check(a.h.tree().node(root + "/detail/button/stage/specimen").has_value(), "one live specimen initially");
        check(!a.h.tree().node(root + "/detail/button/code") && !a.h.tree().node(root + "/detail/button/options"), "details start closed");
        Invocation visitor; visitor.actor = {"agent", "visitor", {}};
        auto act = [&](const std::string& id, std::string_view action, Value args = {}) {
            visitor.args = std::move(args);
            check(a.h.tree().act(id, action, visitor).exit_code() == std::optional<int>(0), id + " " + std::string(action));
            a.h.settle(); a.h.advance(seconds(0.1));
        };
        act(overview + "/filter/search", "set_value", Value{}.set("value", Value("BADGE")));
        check_eq(a.text(overview + "/results/count"), std::string("1 / 36"));
        act(overview + "/results/r0/badge/heading/open", "activate");
        const std::string detail = root + "/detail/badge";
        check_eq(a.h.runtime().route(), std::string("components"));
        check(!a.h.tree().node(detail + "/panes/code"), "choosing a preview does not open the API reference");
        act(detail + "/stage/actions/details", "activate");
        check_eq(a.h.runtime().route(), std::string("components/badge"));
        check(a.h.tree().node(detail + "/stage/specimen").has_value());
        check(!a.h.tree().node(detail + "/code"), "source is optional");
        act(detail + "/panes/code", "activate");
        check_eq(a.text(detail + "/code/scroll/source"), std::string("badge({.count = 3, .id = \"mail\", .tone = BadgeTone::danger})"));
        for (const auto& [language, expected] : {
                 std::pair{"python", "omui.badge(id=\"mail\", count=3, tone=\"danger\")"},
                 std::pair{"lua", "omui.Badge { id = \"mail\", count = 3, tone = \"danger\" }"},
                 std::pair{"ts", "badge({ id: \"mail\", count: 3, tone: \"danger\" })"}}) {
            act(detail + "/code/language/" + language, "activate");
            check_eq(a.text(detail + "/code/scroll/source"), std::string(expected));
        }
        act(detail + "/code/copy", "activate");
        check_eq(a.h.runtime().clipboard_text(), a.text(detail + "/code/scroll/source"));
        check_eq(a.text(detail + "/code/copied"), std::string("Expression copied"));
        act(detail + "/panes/semantics", "activate");
        const std::string semantics = a.text(detail + "/semantics/scroll/json");
        check(semantics.contains("specimen") && semantics.contains("mail"), "the actual specimen's semantic nodes");
        act(root + "/detail/directory/all", "activate");
        check_eq(a.text(overview + "/results/count"), std::string("1 / 36"));
        act(overview + "/filter/search", "set_value", Value{}.set("value", Value("no-such-component")));
        check(a.h.tree().node(overview + "/results/empty").has_value(), "useful empty search");
        check(a.h.tree().node(detail + "/stage/specimen").has_value(), "an empty directory keeps the current live specimen");
        act(overview + "/filter/clear", "activate");
        act(overview + "/results/r0/button/heading/open", "activate");
        const std::string button = root + "/detail/button";
        act(button + "/stage/specimen/variants/primary", "activate");
        check_eq(a.text(button + "/stage/specimen/variants/primary"), std::string("Saved 1"));
        act(button + "/stage/actions/reset", "activate");
        check_eq(a.text(button + "/stage/specimen/variants/primary"), std::string("Save"));
        act(button + "/stage/actions/details", "activate");
        act(button + "/stage/actions/variants", "activate");
        auto tree = [&] {
            std::vector<std::string> out;
            for (const auto& n : a.h.tree().snapshot({.root = button + "/stage/specimen"}).nodes) out.push_back(std::format("{} {} {}", n.id, spec::name(n.role), n.name));
            return out;
        };
        const auto plain = tree();
        const auto chrome = a.pixels_in({0, 0, 1280, 56});
        const auto shown = a.pixels_of(button + "/stage/specimen");
        act(button + "/stage/actions/states", "activate");
        act(button + "/options/style", "set_value", Value{}.set("value", Value("neon")));
        a.h.advance(seconds(1));
        check(a.pixels_of(button + "/stage/specimen") != shown, "the specimen in another style");
        check(a.pixels_in({0, 0, 1280, 56}) == chrome, "the site's bar as it was");
        check(tree() == plain, "only specimen pixels change");
        act(button + "/options/disabled", "toggle");
        check(a.h.tree().node(button + "/stage/specimen/variants/primary")->states.has(State::disabled), "disabled state reaches the real sample");
        visitor.args = {};
        check(a.h.tree().act(button + "/stage/specimen/variants/primary", "activate", visitor).exit_code() != std::optional<int>(0), "disabled blocks real activation");
        act(button + "/stage/actions/reset", "activate");
        check(!a.h.tree().node(button + "/stage/specimen/variants/primary")->states.has(State::disabled), "reset restores enabled");
        check(a.h.tree().node(button + "/stage/specimen/variants/off")->states.has(State::disabled), "the always-disabled variant stays disabled");
        act(button + "/panes/semantics", "activate");
        act(button + "/stage/specimen/variants/primary", "activate");
        check(a.text(button + "/semantics/scroll/json").contains("Saved 1"), "semantics follows an actual specimen action");
        a.h.advance(seconds(1));
        const auto quiet = a.h.step();
        check(quiet.demand.idle() || quiet.demand.max_hz <= 10, "live semantics does not create a frame feedback loop");
        a.h.runtime().set_route("zh/components/text-box"); a.h.settle(); a.h.advance(seconds(0.2));
        check_eq(a.text(root + "/detail/text_box/title"), std::string("文本框"));
        check_eq(a.h.runtime().route(), std::string("zh/components/text-box"));
    });

    s.test("all component addresses mount alone, come to rest and have a readable static destination", [] {
        Site a("components");
        const auto html = site::static_html("en");
        for (const auto& component : site::component_catalog) {
            const auto route = site::canonical_route("components", "en", component.id);
            const auto parsed = site::parse_site_route(route);
            check(parsed && parsed->component == component.id, route);
            check(html.contains("id=\"/" + route + "\""), route + " has static content");
            a.h.runtime().set_route(route); a.h.settle(); a.h.advance(seconds(0.5));
            const auto id = "main/body/view/components/page/gallery/detail/" + std::string(component.id);
            check_eq(a.text(id + "/title"), std::string(component.en));
            check(a.h.tree().node(id + "/stage/specimen").has_value(), route);
            const auto frame = a.h.step();
            check(frame.demand.idle() || frame.demand.max_hz <= 10, route + " rests");
        }
        const auto multiline = site::code_of("cpp", {"code_editor", {{"text", Value("a\nb\t\"c\"")}}});
        check(multiline.contains("a\\nb\\t\\\"c\\\""), "multiline API text is escaped");
        check(!site::parse_site_route("components/unknown"));
    });

    s.test("component layouts: real controls fit phone, tablet and desktop in both languages", [] {
        const auto dir = scratch_dir("site-catalog");
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (std::string_view language : {"en", "zh"}) {
            Headless h{HeadlessOptions{.size = {width, 844}}};
            auto spec = site::app("components", language); h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
            for (std::string_view component : {"", "button", "text_box", "data_grid", "code_editor"}) {
                h.runtime().set_route(site::canonical_route("components", language, component)); h.settle(); h.advance(seconds(0.3));
                const auto nodes = h.tree().snapshot({.root = "main/body/view/components/page"}).nodes;
                for (const auto& node : nodes) if ((node.role == Role::button || node.role == Role::text_box) && node.bounds &&
                        !node.states.has(State::hidden) && !node.states.has(State::offscreen))
                    check(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f,
                          std::format("{} {} {} at {}: {} + {}", component, language, node.id, width, node.bounds->x, node.bounds->width));
                if ((width == 390 && language == "zh") || (width == 1280 && language == "en"))
                    check(h.save_png(dir / std::format("{}-{}-{}.png", component.empty() ? "overview" : component, language, static_cast<int>(width))).has_value());
            }
        }
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
        a.inspect();
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

    s.test("the route catalog: canonical addresses, existing links, languages and missing routes", [] {
        for (const auto& p : site::places()) for (std::string_view language : {"en", "zh"}) {
            for (const std::string address : {site::canonical_route(p.id, language), std::string(language) + "/" + std::string(p.id)}) {
                const auto parsed = site::parse_site_route("#/" + address + "/");
                check(parsed && parsed->place == p.id && parsed->language == language, address);
            }
        }
        check(!site::parse_site_route("docs/not-a-guide"));
        Site a("#/zh/docs/features");
        check(a.shown("features") && a.text("main/bar/nav/to_docs") == "文档");
        a.h.runtime().set_route("zh/brand"); a.h.settle(); a.h.advance(seconds(0.1));
        check(a.shown("brand"), "old brand links remain valid");
        check_eq(a.h.runtime().route(), std::string("zh/brand"));
        a.h.runtime().set_route("zh/missing"); a.h.settle(); a.h.advance(seconds(0.1));
        check(a.shown("not_found"), "unknown routes have an error page");
        check(a.snapshot("not_found").json().contains("zh/missing"));
        check(a.h.click("main/bar/lang")); a.h.settle();
        check_eq(a.h.runtime().route(), std::string("missing"));
        check(a.snapshot("not_found").json().contains("This page isn't here."));
        check(a.h.click("main/bar/lang")); a.h.settle();
        check_eq(a.h.runtime().route(), std::string("zh/missing"));
        check(a.navigate("home")); a.h.settle(); check(a.shown("home"));
    });

    s.test("documentation: searchable guides, selected navigation, retained query and separate reading", [] {
        Site a("docs");
        const std::string read = "main/body/view/docs/page/read/";
        check(a.h.tree().node(read + "results/hello/open").has_value());
        check(a.h.tree().node(read + "results/features/open").has_value());
        check(a.h.tree().node("main/bar/nav/to_docs")->states.has(State::selected));
        Invocation input; input.args = Value{}.set("value", "mcpp new");
        check(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.tree().node(read + "results/hello/open").has_value(), "search includes guide content");
        input.args = Value{}.set("value", "no-such-guide-12345");
        check(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.tree().node(read + "results/empty").has_value());
        check(!a.h.tree().node(read + "results/hello/open"));
        check(a.navigate("explore")); a.h.settle(); check(a.navigate("docs")); a.h.settle();
        check(a.h.tree().node(read + "results/empty").has_value(), "query survives leaving the page");
        input.args = Value{}.set("value", "");
        check(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        check(a.h.click(read + "results/features/open")); a.h.settle();
        check(a.shown("features") && a.h.tree().node("main/body/view/features/page/directory/to_features")->states.has(State::selected));
        check(!a.h.tree().node("main/body/view/home/page/doc"));
        check(a.h.save_png(scratch_dir("site-structure") / "docs-reading.png").has_value());
    });

    s.test("exploration: each exhibit is a working program, the mark returns home, tools are opt-in", [] {
        Site a("explore");
        check(!a.h.tree().node("main/foot/lens_tree"));
        check(a.h.save_png(scratch_dir("site-structure") / "explore.png").has_value());
        check(a.navigate("brand")); a.h.settle();
        check(a.h.tree().node("main/body/view/brand/page/identity/controls/assemble").has_value());
        check(a.h.tree().node("main/bar/nav/to_explore")->states.has(State::selected));
        check(a.navigate("home")); a.h.settle();
        check(a.h.tree().node("main/body/view/home/page/landing").has_value(), "the mark returns to the simplified home");
        check(a.navigate("release")); a.h.settle();
        Invocation scroll; scroll.args = Value{}.set("x", 0.0).set("y", 400.0);
        check(a.h.tree().act("main/body/view/release", "scroll_to", scroll).exit_code() == std::optional<int>(0)); a.h.settle();
        const float before = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/release/page/hero")).y;
        check(a.navigate("docs")); a.h.settle(); check(a.navigate("release")); a.h.settle();
        const float after = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/release/page/hero")).y;
        check(std::abs(before - after) < 1, std::format("page scroll restored: {} vs {}", before, after));
        a.inspect(); check(a.h.click("main/foot/lens_tree")); a.h.settle();
        check(a.h.click("main/foot/tools_toggle")); a.h.settle();
        check(!a.h.tree().node("main/foot/lens_tree") && !a.visible("main/body/view/wireframe"));
    });

    s.test("the new page roles: desktop and phone, English and Chinese, controls stay within the viewport", [] {
        const auto dir = scratch_dir("site-structure");
        for (const bool phone : {false, true}) for (std::string_view lang : {"en", "zh"})
            for (std::string_view place : {"home", "release", "docs", "explore", "hello", "features"}) {
                const Size size = phone ? Size{390, 844} : Size{1280, 800};
                Headless h{HeadlessOptions{.size = size}};
                AppSpec spec = site::app(place, lang); h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
                const std::string root = std::format("main/body/view/{}/page", place);
                check(h.tree().node(root).has_value(), std::format("{} {} {}", place, lang, phone));
                for (const auto& n : h.tree().snapshot({.root = root}).nodes) {
                    if ((n.role == Role::button || n.role == Role::text_box) && n.bounds && !n.states.has(State::hidden) && !n.states.has(State::offscreen))
                        check(n.bounds->x >= -0.5f && n.bounds->x + n.bounds->width <= size.width + 0.5f,
                              std::format("{} {} {} inside: {} + {}", place, lang, n.id, n.bounds->x, n.bounds->width));
                }
                check(h.save_png(dir / std::format("{}-{}-{}.png", place, lang, phone ? "phone" : "desktop")).has_value());
            }
    });

    s.test("the static page: every place's HTML, md4c-html's bytes, in a page that hides it once the program runs", [] {
        for (std::string_view lang : {"en", "zh"}) {
            const std::string html = site::static_html(lang);
            check(html.starts_with("<main id=\"omui-static\""), lang);
            const auto skip = html.find("<a class=\"skip\" href=\"#/"), nav = html.find("<nav aria-label=");
            check(skip != std::string::npos && skip < nav, std::format("{}: a skip link before the places", lang));
            for (const site::Place& p : site::places()) check(html.find(std::format("<section id=\"/{}\"", p.id == "home" ? (lang == "zh" ? "zh/home" : "home") : site::canonical_route(p.id, lang))) != std::string::npos, std::format("{} {}", lang, p.id));
            check(html.find(md::to_html(md::parse(site::document("features", lang)))) != std::string::npos, "the features as md4c-html writes them");
        }
        const std::string en = site::static_html("en");
        check(en.find("aria-label=\"One interface. Every perspective.\"") != std::string::npos && en.find("One interface.<br>Every perspective.") != std::string::npos, "the thesis and deliberate line break, readable without JavaScript");
        // The site's own part pinned (the docs change with the framework; they are held to md4c-html's bytes above).
        auto own = [](const std::string& html) { return html.substr(0, html.find("<section id=\"/", html.find("<section id=\"/") + 1)); };
        std::println("    static en {:016x} zh {:016x}", fnv(own(en)), fnv(own(site::static_html("zh"))));
        check_eq(std::format("{:016x}", fnv(own(en))), std::string("85286cf4ad05c4b5"));
        check_eq(std::format("{:016x}", fnv(own(site::static_html("zh")))), std::string("efc510de8e1f2947"));

        const auto out = site::inject(page, en, "zh");
        check(out.has_value(), "dist-toy's page");
        const std::string& p = *out;
        check(p.find("<html lang=\"zh-CN\">") != std::string::npos, "the page's language");
        const auto main = p.find("<main id=\"omui-static\""), canvas = p.find("<canvas id=\"canvas\""), hide = p.find("window.omuiHasPresented"),
                   loader = p.find("<script src=\"omui-site.js\">");
        check(main < canvas, "the static page before the canvas");
        check(hide != std::string::npos && hide < loader, "hidden by a script before the program's");
        check(p.find("#omui-static{") < p.find("</head>"), "its style in the head");
        check(p.find("@font-face{font-family:'Inter';src:url(assets/4304c4ca3090.ttf)") != std::string::npos, "in the program's own font");
        const auto no_script = p.find("<noscript><style>"), no_script_end = p.find("</style></noscript>", no_script);
        const auto font_face = p.find("@font-face");
        check(no_script < font_face && font_face < no_script_end, "URL fonts are only a no-JavaScript fallback; active JavaScript shares managed bytes");
        check(p.contains("id=\"omui-static-fonts\" type=\"application/json\""), "static font consumers get inert descriptors before bootstrap");
        check(p.contains("\"name\":\"Inter.ttf\"") && p.contains("\"when\":\"body\""), "the body face is declared by the same selected font list");
        check(p.contains("#omui-static>nav[data-default]{display:flex}"), "the selected static navigation keeps its layout");
        std::string initial_page(page);
        const auto font_url = initial_page.find("\"assets/4304c4ca3090.ttf\"");
        initial_page.replace(font_url, std::string_view("\"assets/4304c4ca3090.ttf\"").size(), "\"assets/full.ttf\",[],\"assets/initial.ttf\"");
        const auto initial_out = site::inject(initial_page, en, "en");
        check(initial_out && initial_out->contains("font-family:'Inter';src:url(assets/initial.ttf)"), "the readable opening uses its initial face instead of a second full-face request");
        check(!site::inject("<html><body>nothing</body></html>", en, "en").has_value(), "a page dist-toy did not write");
        check(!site::inject(p, en, "en").has_value(), "a page already pre-rendered");
        const auto openings = site::inject(page, en, "en", R"({"version":1,"previews":[],"note":"</script><script>bad()</script>"})");
        check(openings && openings->contains("id=\"omui-first-frames\""), "build-time asset references precede the bootstrap");
        check(openings && !openings->contains("<script>bad()"), "asset catalogue data cannot close its script element");
        check(!site::inject(page, en, "en", "not json"), "malformed opening metadata is rejected");
        check(!site::inject(page, en, "en", R"({"version":2,"previews":[]})"), "unknown opening metadata version is rejected");
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
        const Rect live = h->runtime().screen_bounds(h->runtime().find("main/foot/tools_toggle"));
        check(live.height < 60 && live.x + live.width <= 390, std::format("the foot's Live on one line, inside ({} high, to {})", live.height, live.x + live.width));
    });

    s.test("a panel opened beside the place: the place is laid out again to the room left", [] {
        Site a;
        // Landmarks a screen reader jumps between: the places' navigation, the place itself.
        check(a.h.tree().node("main/bar/nav") && a.h.tree().node("main/bar/nav")->role == Role::navigation, "the navigation landmark");
        check(a.h.tree().node("main/bar/mark")->states.has(State::selected) && !a.h.tree().node("main/bar/nav/to_docs")->states.has(State::selected),
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
