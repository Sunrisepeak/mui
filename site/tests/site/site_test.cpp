// The site as a reader, an agent and a crawler meet it: each place shown in
// turn, in both languages; the components with their code and the style packs; the lens moving the same page from its pixels to
// the tree's wireframe, the JSON an agent reads and the assertions a test
// writes; the home page's tree hash pinned (what the footer compares in the
// browser); the live timeline editor edited and undone; the static HTML a
// reader without JavaScript gets, written into dist-toy's page.
import std;
import omui;
import omui.site;
import omui.ui.markdown;  // openmui.markdown with it
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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
    void inspect_object() { if (!h.tree().node("main/body/view/release/page/hero/demo/renderings")) { expect(h.click("main/body/view/release/page/hero/demo/context/inspect")); h.settle(); } }
    void inspect() { if (!h.tree().node("main/foot/lens_tree")) { expect(h.click("main/foot/tools_toggle")); h.settle(); } }
    bool navigate(std::string_view place) {
        if (place == "home") return h.click("main/bar/mark");
        if (place == "hello") return h.click("main/bar/start");
        // The reference pages, from the docs' list.
        if (place == "motion" || place == "styles" || place == "interface") {
            if (!h.click("main/bar/nav/to_docs")) return false;
            h.settle();
            const auto open = std::format("main/body/view/docs/page/read/results/{}/open", place);
            if (h.tree().act(open, "scroll_into_view").exit_code() != std::optional<int>(0)) return false;
            h.settle(seconds(0.5));
            return h.click(open);
        }
        if (place == "features" || place == "practices") {
            if (!h.click("main/bar/nav/to_docs")) return false;
            h.settle();
            return h.click(std::format("main/body/view/docs/page/directory/to_{}", place));
        }
        if (place == "brand" || place == "timeline" || place == "reactions" || place == "release") {
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

std::uint64_t fnv(std::string_view s) { return mlibs::hash::fnv1a(s); }

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
<script defer src="omui-site.js"></script>
</body>
</html>
)";

}  // namespace

static UTest cases{"site", [] {
    "only documentation has Markdown, in both languages; the home and programs have their own views"_test = [] {
        for (const site::Place& p : site::places()) {
            if (!site::is_document(p.id) || p.id == "docs") {
                expect(site::document(p.id, "en").empty()) << std::format("{} is a live page, not a document", p.id);
                continue;
            }
            for (std::string_view lang : {"en", "zh"}) {
                const std::string_view doc = site::document(p.id, lang);
                // Home's title is the opening's claim (hero.cpp); its document begins a level down.
                expect(doc.starts_with(p.id == "hello" ? "## " : "# ")) << std::format("{} in {}: a document with its title", p.id, lang);
                expect(!mlibs::markdown::parse(doc).root.children.empty()) << std::format("{} in {}: reads as Markdown", p.id, lang);
            }
        }
        expect(site::document("hello", "zh").contains("mcpp")) << "the Chinese quickstart";
        expect(site::document("nowhere", "en").empty());
    };

    "each place shown in turn: one at a time, its tree under the view"_test = [] {
        Site a;
        expect(a.shown("home")) << "home first";
        expect(!a.shown("features")) << "one place at a time";
        for (const site::Place& p : site::places()) {
            if (!p.navigation) continue;  // the mark is reached through the brand link (brand_test)
            expect(a.navigate(p.id)) << std::format("the nav's {}", p.id);
            a.h.settle();
            for (const site::Place& q : site::places())
                expect(a.shown(q.id) == (p.id == q.id)) << std::format("{} shown: {}", p.id, q.id);
            const std::size_t nodes = a.snapshot(p.id).get("nodes")->list()->size();
            expect(nodes >= 3u) << std::format("{}: its tree ({} nodes)", p.id, nodes);
        }
    };

    "the language: the bar, the nav and the documents in Chinese, the same ids"_test = [] {
        Site a;
        std::vector<std::string> ids;
        for (const auto& n : a.h.tree().snapshot({}).nodes) ids.push_back(n.id);
        expect(eq(a.text("main/bar/nav/to_docs"), std::string("Docs")));
        expect(a.h.click("main/bar/lang"));
        a.h.settle();
        expect(eq(a.text("main/bar/nav/to_docs"), std::string("文档")));
        expect(eq(a.text("main/bar/lang"), std::string("English")));
        const std::string doc = a.snapshot("home").json();
        expect(doc.find("一个界面，不止一种可能。") != std::string::npos) << "the Chinese home page";
        // The ids are the structure's, not the text's: the same nodes in Chinese.
        std::vector<std::string> zh;
        for (const auto& n : a.h.tree().snapshot({}).nodes) zh.push_back(n.id);
        std::set<std::string> was(ids.begin(), ids.end());
        std::size_t kept = 0;
        for (const auto& id : zh) kept += was.contains(id);
        expect(kept * 10 >= zh.size() * 9) << std::format("{} of {} ids the same", kept, zh.size());
    };

    "switching the language: the letters gather into the other language, and end as the page drawn in it"_test = [] {
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
        expect(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(0.12));
        const auto moving = a.page();
        expect(moving != direct) << "under way: the letters still gathering";
        const auto switched_tree = a.snapshot("home"), direct_tree = zh.snapshot("home");
        if (site::tree_hash(switched_tree) != site::tree_hash(direct_tree)) {
            const auto dir = scratch_dir("site-language-tree");
            std::ofstream(dir / "switched.json") << switched_tree.json();
            std::ofstream(dir / "direct.json") << direct_tree.json();
            std::println("    language semantic snapshots: {}; focus switched {}, direct {}", dir.string(),
                         a.h.runtime().path(a.h.runtime().focused()), zh.h.runtime().path(zh.h.runtime().focused()));
        }
        expect(site::tree_hash(switched_tree) == site::tree_hash(direct_tree)) << "the tree is already the other language's: the motion is only paint";
        a.h.settle(seconds(3));
        (void)a.h.save_png(scratch_dir("site-sw") / "a.png");
        if (a.page() != direct) {  // where they differ, to look at
            const auto& one = a.h.pixels();
            const auto& two = zh.h.pixels();
            int left = one.width(), top = one.height(), right = -1, bottom = -1;
            for (int y = 0; y < std::min(one.height(), two.height()); ++y)
                for (int x = 0; x < std::min(one.width(), two.width()); ++x)
                    if (std::memcmp(&one.row(y)[x], &two.row(y)[x], 4) != 0) left = std::min(left, x), right = std::max(right, x), top = std::min(top, y), bottom = std::max(bottom, y);
            std::println("    language pixels differ in [{}, {}] – [{}, {}]", left, top, right, bottom);
            (void)zh.h.save_png(scratch_dir("site-sw-direct") / "direct.png");
        }
        expect(a.page() == direct) << "at rest: the very pixels of the page opened in Chinese (above the foot, where the switch is marked)";
        expect(site::tree_hash(a.snapshot("home")) == site::tree_hash(direct_tree)) << "the semantic tree also remains the directly opened Chinese tree at rest";
    };

    "the release scene: a checklist as a person sees it, and its node as a screen reader says it, an agent sends it, a test asserts it"_test = [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero";
        expect(!a.h.tree().node(hero + "/demo/renderings")) << "technical views start closed"; a.inspect_object();
        const auto ship = a.h.tree().node(hero + "/demo/specimen/actions/ship");
        expect(ship.has_value() && ship->role == Role::button && ship->states.has(State::disabled)) << "Ship 0.1 waits for its checks";
        const std::string panel = hero + "/demo/renderings/";
        expect(eq(a.text(panel + "reader/body/line"), std::string("\"Record release\", button, dimmed")));
        expect(a.text(panel + "agent/body/line").find("\"act\": \"specimen/actions/ship\", \"action\": \"activate\"") != std::string::npos) << a.text(panel + "agent/body/line");
        expect(a.text(panel + "test/body/line").find("check(node(\"specimen/actions/ship\").states.has(\"disabled\"));") != std::string::npos) << a.text(panel + "test/body/line");
        // A person checks one: the panels follow it, read again from the tree.
        expect(a.h.click(hero + "/demo/specimen/items/docs/done"));
        a.h.advance(seconds(0.5));
        expect(eq(a.text(panel + "reader/body/line"), std::string("\"Docs reviewed\", checkbox, checked")));
        expect(a.text(panel + "test/body/line").find("check(node(\"specimen/items/docs/done\").states.has(\"checked\"));") != std::string::npos) << a.text(panel + "test/body/line");
    };

    "the release workspace: add, rename, remove, undo, reset and share the same data with the motion scene"_test = [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen", shape = "main/body/view/release/page/motion/shown/shape";
        Invocation person;
        auto act = [&](std::string id, std::string_view action, Value args = {}) { person.args = std::move(args); expect(a.h.tree().act(id, action, person).exit_code() == std::optional<int>(0)) << id + " " + std::string(action); a.h.settle(); };
        act(board + "/add/submit", "activate");
        expect(a.text(board + "/validation").contains("name")) << "empty names get a visible error";
        act(board + "/add/title", "set_value", Value{}.set("value", "  Check the keyboard journey  "));
        act(board + "/add/submit", "activate");
        expect(eq(a.text(board + "/items/task-1/done"), std::string("Check the keyboard journey")));
        expect(eq(a.text(shape + "/task-1/title"), std::string("Check the keyboard journey")));
        expect(a.h.tree().node(board + "/items/task-1/done")->origin == spec::Origin::user_content);
        expect(a.h.tree().node(shape + "/task-1/title")->origin == spec::Origin::user_content);
        act(board + "/add/title", "set_value", Value{}.set("value", "A task entered with the keyboard"));
        a.h.runtime().focus(a.h.runtime().find(board + "/add/title"));
        a.h.key(Key::enter); a.h.settle();
        expect(a.h.tree().node(board + "/items/task-2/done").has_value()) << "Enter submits the task";
        act(board + "/history/undo", "activate");
        expect(!a.h.tree().node(board + "/items/task-2/done")) << "keyboard submission joins the same undo history";
        act(board + "/items/task-1/done", "toggle");
        expect(eq(a.text(shape + "/task-1/state"), std::string("Complete")));
        act(board + "/items/task-1/edit", "activate");
        expect(a.h.runtime().path(a.h.runtime().focused()) == board + "/editor/title") << "editing focuses the real title field";
        act(board + "/editor/title", "set_value", Value{}.set("value", "Review the keyboard and focus paths"));
        act(board + "/editor/actions/save", "activate");
        expect(eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths")));
        expect(a.h.runtime().path(a.h.runtime().focused()) == board + "/items/task-1/edit") << "save restores focus to the task";
        act(board + "/history/undo", "activate");
        expect(eq(a.text(board + "/items/task-1/done"), std::string("Check the keyboard journey")));
        act(board + "/items/task-1/edit", "activate");
        act(board + "/editor/actions/cancel", "activate");
        act(board + "/history/redo", "activate");
        expect(eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths")));
        act(board + "/items/task-1/edit", "activate");
        act(board + "/editor/actions/remove", "activate");
        expect(!a.h.tree().node(board + "/items/task-1/done"));
        expect(!a.h.tree().node(shape + "/task-1/title"));
        act(board + "/history/undo", "activate");
        expect(a.h.tree().node(board + "/items/task-1/done")->states.has(State::checked));
        act(board + "/actions/reset", "activate");
        expect(!a.h.tree().node(board + "/items/task-1/done"));
        act(board + "/history/undo", "activate");
        expect(eq(a.text(board + "/items/task-1/done"), std::string("Review the keyboard and focus paths")));
        expect(a.navigate("docs")); a.h.settle(); expect(a.navigate("release")); a.h.settle();
        expect(eq(a.text(shape + "/task-1/title"), std::string("Review the keyboard and focus paths")));
        expect(a.h.click("main/bar/lang")); a.h.advance(seconds(1));
        expect(eq(a.text(board + "/items/task-1/done"), std::string("Review the keyboard and focus paths")));
        expect(eq(a.text(board + "/items/docs/done"), std::string("文档已审阅")));
        expect(eq(a.text(shape + "/task-1/state"), std::string("已完成")));
        act(board + "/history/copy", "activate");
        expect(a.h.runtime().clipboard_text().starts_with("# 发布清单\n"));
        expect(a.h.runtime().clipboard_text().contains("- [x] Review the keyboard and focus paths\n"));
        expect(eq(a.text(board + "/history/copy"), std::string("清单已复制")));
        expect(a.h.save_png(workspace_media() / "edited-zh.png").has_value());
    };

    "workspace layouts: five widths, both languages, editable long names and optional technical views"_test = [] {
        const auto dir = workspace_media();
        std::map<std::string, std::uint64_t, std::less<>> baseline;
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (std::string_view lang : {"en", "zh"}) {
            Headless h{HeadlessOptions{.size = {width, 844}}}; auto spec = site::app("release", lang);
            h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
            const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen";
            auto inside = [&] {
                for (const auto& n : h.tree().snapshot({.root = hero}).nodes) if ((n.role == Role::button || n.role == Role::text_box || n.role == Role::checkbox) && n.bounds && !n.states.has(State::hidden) && !n.states.has(State::offscreen))
                    expect(n.bounds->x >= -0.5f && n.bounds->right() <= width + 0.5f) << std::format("{} {} at {}: {} + {}", lang, n.id, width, n.bounds->x, n.bounds->width);
            };
            inside();
            const auto hash = site::tree_hash(h.tree().snapshot({.root = "main/body/view/release/page"}).to_value());
            const auto [same, added] = baseline.try_emplace(std::string(lang), hash);
            expect(added || same->second == hash) << std::format("{} at {} keeps the same semantic tree: {:016x} / {:016x}", lang, width, same->second, hash);
            const bool capture = (width == 390 && lang == "zh") || (width == 1280 && lang == "en");
            if (capture) expect(h.save_png(dir / std::format("release-{}-{}.png", lang, static_cast<int>(width))).has_value());
            Invocation input; input.args = Value{}.set("value", lang == "zh" ? "和设计与无障碍团队一起检查这一轮发布的键盘导航和焦点恢复" : "Review this release with the design and accessibility teams, including keyboard navigation");
            expect(h.tree().act(board + "/add/title", "set_value", input).exit_code() == std::optional<int>(0)); h.settle();
            expect(h.tree().act(board + "/add/submit", "activate").exit_code() == std::optional<int>(0)); h.settle();
            expect(h.tree().act(board + "/items/task-1/done", "scroll_into_view").exit_code() == std::optional<int>(0)); h.settle(); inside();
            expect(h.tree().act(board + "/items/task-1/edit", "activate").exit_code() == std::optional<int>(0)); h.settle(); inside();
            expect(h.tree().act(hero + "/demo/context/inspect", "activate").exit_code() == std::optional<int>(0)); h.settle(); inside();
            if (capture) expect(h.save_png(dir / std::format("technical-{}-{}.png", lang, static_cast<int>(width))).has_value());
        }
    };

    "the task view changes layouts with stable ids, can be interrupted and works with motion off"_test = [] {
        Site a("release"); const std::string scene = "main/body/view/release/page/motion/shown", shape = scene + "/shape";
        auto act = [&](std::string id, std::string_view action) { expect(a.h.tree().act(id, action).exit_code() == std::optional<int>(0)) << id; a.h.settle(); };
        act(scene + "/controls/change", "scroll_into_view"); a.h.advance(seconds(0.3));
        act(scene + "/controls/follow", "toggle");
        if (a.text(scene + "/controls/change") == "Show list") act(scene + "/controls/change", "activate");
        a.h.advance(seconds(1));
        act(shape, "scroll_into_view"); a.h.advance(seconds(0.2));
        const auto rows = a.h.tree().snapshot({.root = shape}).nodes;
        const auto wide = a.h.runtime().bounds(a.h.runtime().find(shape + "/tests"));
        const auto list_pixels = a.pixels_in(a.h.runtime().screen_bounds(a.h.runtime().find(shape)));
        expect(a.h.save_png(workspace_media() / "motion-list.png").has_value());
        const auto list_page = a.page();
        expect(a.h.tree().act(scene + "/controls/change", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.1));
        const auto moving_page = a.page();
        expect(a.h.step().animating) << "the interruption is issued while the transition is active";
        expect(a.h.save_png(workspace_media() / "motion-between.png").has_value());
        expect(a.h.tree().act(scene + "/controls/change", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(1));
        expect(std::abs(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width - wide.width) < 1) << "an interrupted change returns to the list";
        act(scene + "/controls/change", "activate"); a.h.advance(seconds(1));
        const auto cards = a.h.tree().snapshot({.root = shape}).nodes;
        std::vector<std::string> before, after;
        for (const auto& n : rows) before.push_back(n.id); for (const auto& n : cards) after.push_back(n.id);
        expect(before == after) << "task ids and descendants remain stable across the two layouts";
        expect(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width < wide.width) << "the cards use multiple columns";
        expect(a.pixels_in(a.h.runtime().screen_bounds(a.h.runtime().find(shape))) != list_pixels) << "the real task scene changes its layout";
        expect(moving_page != list_page && moving_page != a.page()) << "the sampled middle frame differs from both endpoint drawings";
        expect(a.h.save_png(workspace_media() / "motion-cards.png").has_value());
        a.h.runtime().set_motion_level(MotionLevel::off);
        act(scene + "/controls/change", "activate"); a.h.advance(seconds(0.1));
        expect(std::abs(a.h.runtime().bounds(a.h.runtime().find(shape + "/tests")).width - wide.width) < 1) << "motion off still reaches the chosen layout";
        a.h.runtime().set_motion_level(MotionLevel::full);
        act(scene + "/controls/follow", "toggle"); a.h.advance(seconds(0.5));
        expect(a.h.runtime().motion_time(a.h.runtime().find(shape)).has_value()) << "scroll seeking restarts after manual mode";
        expect(a.navigate("docs")); a.h.settle(); expect(a.navigate("release")); a.h.settle(); a.h.advance(seconds(0.5));
        expect(a.h.runtime().motion_time(a.h.runtime().find(shape)).has_value()) << "restored scroll binds the new scene after a page visit";
    };

    "the simulated agent uses edited task ids, and leaving the page cancels its pending run"_test = [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero", board = hero + "/demo/specimen";
        Invocation person; person.args = Value{}.set("value", "Check the new task");
        expect(a.h.tree().act(board + "/add/title", "set_value", person).exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.tree().act(board + "/add/submit", "activate").exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(7));
        expect(a.h.tree().node(board + "/items/task-1/done")->states.has(State::checked));
        expect(a.text(board + "/shipped").starts_with("Shipped"));
        expect(a.h.tree().act(board + "/actions/reset", "activate").exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.2));
        expect(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0)); a.h.advance(seconds(1));
        expect(!a.h.tree().node(board + "/items/tests/done")->states.has(State::checked)) << "stopping cancels the pending scripted step";
        expect(a.h.tree().act(hero + "/agent_row/let_agent", "activate").exit_code() == std::optional<int>(0));
        expect(a.navigate("docs")); a.h.advance(seconds(6));
        expect(a.navigate("release")); a.h.settle();
        expect(!a.h.tree().node(board + "/items/tests/done")->states.has(State::checked)) << "cancelled timer does not edit after navigation";
        expect(!a.h.tree().node(hero + "/agent_row/let_agent")->states.has(State::disabled));
    };

    "the release scene's agent ships it: each step a real action as an agent, through the same checks, each one undoable"_test = [] {
        Site a("release");
        const std::string hero = "main/body/view/release/page/hero";
        a.inspect_object();
        expect(a.h.tree().act(hero + "/agent_row/let_agent", "scroll_into_view").exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.click(hero + "/agent_row/let_agent")) << "a person asks the agent";
        a.h.advance(seconds(6));
        for (const char* item : {"tests", "docs", "notes"}) {
            const auto n = a.h.tree().node(hero + "/demo/specimen/items/" + item + "/done");
            expect(n && n->states.has(State::checked)) << std::format("{} checked by the agent", item);
        }
        expect(a.text(hero + "/demo/specimen/shipped").starts_with("Shipped")) << "shipped";
        expect(a.text(hero + "/demo/renderings/agent/body/line").find("done") != std::string::npos) << a.text(hero + "/demo/renderings/agent/body/line");
        const auto agents = std::ranges::count_if(a.h.runtime().action_journal(), [](const ActionEntry& e) { return e.actor.starts_with("agent"); });
        expect(agents >= 4) << std::format("{} actions journaled as the agent's", agents);
        // Every step can be taken back, the shipping too: undo unships it, the panels with it; redo ships it again.
        const auto shipped = [&] { return a.text(hero + "/demo/specimen/shipped").starts_with("Shipped"); };
        expect(eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0}));
        a.h.advance(seconds(0.3));
        expect(!shipped()) << "undone: not shipped";
        expect(a.text(hero + "/demo/renderings/agent/body/line").find("done") == std::string::npos) << "the agent's request is past";
        const auto items_checked = [&] {
            int n = 0;
            for (const char* item : {"tests", "docs", "notes"})
                if (auto x = a.h.tree().node(hero + "/demo/specimen/items/" + item + "/done"); x && x->states.has(State::checked)) ++n;
            return n;
        };
        expect(eq(items_checked(), 3));  // the checks stay: the shipping alone was taken back
        expect(eq(a.h.runtime().redo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0}));
        a.h.advance(seconds(0.3));
        expect(shipped()) << "redone: shipped";
        expect(eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0}));
        expect(eq(a.h.runtime().undo(a.h.runtime().find(hero + "/demo/specimen")).exit_code().value_or(-1), std::int64_t{0}));
        a.h.advance(seconds(0.3));
        expect(!shipped() && items_checked() == 2) << "then the last check";
    };

    "on a phone: one header line, a compact menu, the full-width place and a panel in its place"_test = [] {
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
        expect(shown("main/bar/menu_toggle") && shown("main/foot/tools_toggle")) << "compact navigation and opt-in inspection";
        expect(!h.tree().node("main/foot/lens_tree")) << "no inspector controls on the reading surface";
        const auto brand = h.runtime().screen_bounds(h.runtime().find("main/bar/mark"));
        const auto language = h.runtime().screen_bounds(h.runtime().find("main/bar/lang"));
        expect(std::abs(brand.y - language.y) < 1) << "language stays on the header line";
        expect(h.click("main/bar/menu_toggle")); h.settle();
        expect(shown("main/bar/nav/to_docs")) << "menu exposes the primary destinations";
        const Rect nav = h.runtime().screen_bounds(h.runtime().find("main/bar/nav"));
        expect(nav.x >= 0 && nav.x + nav.width <= 390.5f) << std::format("the places within the phone: {} + {}", nav.x, nav.width);
        const Rect view = h.runtime().screen_bounds(h.runtime().find("main/body/view"));
        expect(view.width == 390) << std::format("the view the whole width: {}", view.width);
        expect(h.tree().act("main/body/view/release/page/hero/demo/context/inspect", "activate").exit_code() == std::optional<int>(0)); h.settle();
        // The renderings' words whole: no line of a card below its card's bottom.
        for (const char* card : {"reader", "agent", "test"}) {
            const std::string base = std::format("main/body/view/release/page/hero/demo/renderings/{}", card);
            expect(h.runtime().valid(h.runtime().find(base + "/body/line"))) << base;
            const Rect c = h.runtime().screen_bounds(h.runtime().find(base));
            const Rect l = h.runtime().screen_bounds(h.runtime().find(base + "/body/line"));
            expect(l.y + l.height <= c.y + c.height + 0.5f) << std::format("{}'s line inside its card: {} vs {}", card, l.y + l.height, c.y + c.height);
        }
        for (const char* id : {"hero/demo/specimen/actions/ship", "hero/agent_row/let_agent", "hero/cta/to_explore"}) {
            const Rect r = h.runtime().screen_bounds(h.runtime().find(std::format("main/body/view/release/page/{}", id)));
            expect(r.x >= 0 && r.x + r.width <= 390) << std::format("{} inside: {} + {}", id, r.x, r.width);
        }
        // A panel takes the place's room; Esc puts it away, wherever the focus is.
        expect(h.click("main/bar/settings_toggle"));
        h.settle();
        expect(shown("main/body/settings") && !shown("main/body/view")) << "the settings in the place's place";
        h.runtime().focus(NodeId{});
        h.key(Key::escape);
        h.settle();
        expect(!shown("main/body/settings") && shown("main/body/view")) << "Esc closed it, the place back";
        expect(h.click("main/bar/menu_toggle")); h.settle();
        expect(h.click("main/bar/nav/to_docs")); h.settle();
        expect(h.click("main/body/view/docs/page/directory/to_features")); h.settle();
        const auto features = h.tree().node("main/body/view/features/page/read/doc");
        expect(features && !features->states.has(State::offscreen)) << "the features, read";
    };

    "the settings: each choice's name drawn and read by it, and each one changing what it says — the theme, the motion, how things move, the effects"_test = [] {
        Site a;
        expect(a.h.click("main/bar/settings_toggle"));
        a.h.settle();
        const std::string at = "main/body/settings/";
        for (const std::string id : {"theme", "motion", "motion_style", "effects", "renderer"}) {
            const auto field = a.h.tree().node(at + id + "_field/" + id), name = a.h.tree().node(at + id + "_field/name");
            expect(field && name && !name->name.empty() && field->name == name->name) << std::format("{}: drawn as it is read", id);
        }
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        auto choose = [&](std::string_view id, std::string_view value) {
            agent.args = Value{}.set("value", std::string(value));
            expect(a.h.tree().act(std::format("{}{}_field/{}", at, id, id), "set_value", agent).exit_code() == std::optional<int>(0)) << std::format("{} {}", id, value);
            a.h.settle(seconds(1));
        };
        const Color light = a.h.runtime().theme().color("background");
        expect(std::abs(light.r - 0xfb / 255.0f) < 0.002f && std::abs(light.b - 0xfa / 255.0f) < 0.002f) << "the site's own theme (site.theme.xdef.ts), not the default it extends";
        choose("theme", "dark");
        const Color dark = a.h.runtime().theme().color("background");
        expect(dark.r + dark.g + dark.b < light.r + light.g + light.b - 1) << "dark";
        choose("motion", "off");
        expect(a.h.runtime().motion_level() == MotionLevel::off);
        choose("motion_style", "ink");
        expect(a.h.runtime().motion_choice() == "ink");
        choose("motion_style", "default");
        expect(a.h.runtime().motion_choice().empty()) << "each control's own again";
        choose("effects", "t1");
        expect(a.h.runtime().adaptation().reason == "the person's setting") << a.h.runtime().adaptation().reason;
        expect(a.h.tree().act(at + "theme_field/theme", "set_value", [&] { agent.args = Value{}.set("value", "sepia"); return agent; }()).exit_code() != std::optional<int>(0)) << "not one of its choices";
    };

    "the lens: pixels, the tree's wireframe, an agent's JSON, a test's assertions — of the same nodes"_test = [] {
        Site a;
        a.inspect();
        const auto plain = a.page();
        expect(!a.visible("main/body/view/wireframe") && !a.visible("main/body/view/lens_agent")) << "only the pixels";
        expect(a.h.click("main/foot/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        expect(a.visible("main/body/view/wireframe")) << "the wireframe over the page";
        expect(a.pixels() != plain) << "the wireframe drawn";
        const auto wire = a.h.tree().node("main/body/view/wireframe");
        const std::int64_t drawn = wire->value.get("nodes") ? wire->value.get("nodes")->as_int() : 0;
        const auto home = a.snapshot("home");
        expect(drawn == static_cast<std::int64_t>(home.get("nodes")->list()->size())) << "every node of the place outlined";

        expect(a.h.click("main/foot/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string json = a.text("main/body/view/lens_agent/agent_text");
        expect(json.starts_with("// openmui.tree@0.1's snapshot of main/body/view/home")) << "the place named once";
        expect(json.find("\"id\":\"…/page/landing/intro/title\"") != std::string::npos) << "the nodes as an agent reads them, from the place";
        for (const Value& n : *home.get("nodes")->list()) {
            std::string id = std::string(n.get("id")->as_string());
            if (id.starts_with("main/body/view/home/")) id = "…/" + id.substr(std::string_view("main/body/view/home/").size());
            expect(json.find(Value(id).json()) != std::string::npos) << std::format("{} in the agent's JSON", id);
        }

        expect(a.h.click("main/foot/lens_test"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string lines = a.text("main/body/view/lens_test/test_text");
        expect(lines.find("tree.node(std::format(\"main/body/view/home/{}\", path))") != std::string::npos) << "the place's nodes found under it";
        expect(lines.find("check_eq(node(\"page/landing/intro/title\").role, \"heading\");") != std::string::npos) << "the title's role asserted";

        expect(a.h.click("main/foot/lens_pixels"));
        a.h.move({4, 796});  // the pointer off the buttons (no hover)
        a.h.settle(seconds(1));
        expect(a.page() == plain) << "back to the pixels, the same pixels (above the foot, where the lenses are marked)";
    };

    "the timeline's monitor: what plays at the playhead, the title fading as its clip does; Play runs the playhead and stops at the end"_test = [] {
        Site a;
        expect(a.navigate("timeline"));
        a.h.settle(seconds(3));
        const std::string base = "main/body/view/timeline/page/";
        expect(eq(a.text(base + "program/monitor/time/row:0/timecode"), std::string("01:15")));
        expect(a.h.click(base + "program/play"));
        a.h.advance(seconds(1));
        const std::string after = a.text(base + "program/monitor/time/row:0/timecode");
        expect(after != "01:15" && after.starts_with("02:")) << after;
        // Sought into the fight: the playhead is the editor's, the monitor follows it.
        Invocation agent;
        agent.actor = {"agent", "test", {}};
        agent.args = Value{}.set("frame", std::int64_t{150});
        expect(a.h.tree().act(base + "editor", "seek", agent).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(0.1));
        expect(a.text(base + "program/monitor/time/row:0/timecode").starts_with("05:")) << a.text(base + "program/monitor/time/row:0/timecode");
    };

    "the home is a short landing with one shared mark and two clear destinations"_test = [] {
        Site a;
        const std::string landing = "main/body/view/home/page/landing";
        const auto title = a.h.tree().node(landing + "/intro/title");
        expect(title && title->role == Role::heading && title->name == "One interface. Every perspective.");
        for (const auto section : {"hero", "agent", "time", "motion", "fast", "doc"})
            expect(!a.h.tree().node(std::string("main/body/view/home/page/") + section)) << "the full workspaces live on their own pages";
        for (const auto part : {"outline", "letter_m", "letter_u", "letter_i"})
            expect(a.h.tree().node(landing + "/scene/visual/mark/" + part + "/path").has_value()) << part;
        expect(a.h.click(landing + "/intro/actions/explore")); a.h.settle();
        expect(a.shown("components"));
        expect(a.navigate("home")); a.h.settle();
        expect(a.h.click(landing + "/intro/actions/creative")); a.h.settle();
        expect(a.shown("explore"));
    };

    "the release scene's Get started action opens the quickstart and real program"_test = [] {
        Site a("release");
        expect(!a.h.tree().node("main/body/view/release/page/doc")) << "the live scene has no long document";
        expect(a.h.click("main/body/view/release/page/hero/cta/to_start"));
        a.h.settle(seconds(2));
        expect(a.shown("hello"));
        const std::string intro = "main/body/view/hello/page/example/intro";
        expect(a.h.tree().act("main/body/view/hello/page/instructions/copy_commands", "activate").exit_code() == std::optional<int>(0));
        expect(eq(a.h.runtime().clipboard_text(), std::string(site::quickstart_commands)));
        expect(!a.h.tree().find({.under = intro + "/run", .role = Role::text_box}).empty()) << "a usable program beside its source";
    };

    "the home page's tree: pinned, the same in a browser as here; boxes do not count"_test = [] {
        Site a;
        a.inspect();
        const auto h = site::tree_hash(a.snapshot("home"));
        std::println("    home tree {:016x}", h);
        expect(h == site::pinned_home_hash) << "the controlled home baseline is pinned";
        expect(a.text("main/foot/proof/hash") == std::format("tree {:08x}", h >> 32)) << "the footer describes the visitor's current tree";
        Value changed_boxes = a.snapshot("home");
        Value::List moved = *changed_boxes.get("nodes")->list();
        for (auto& node : moved) node.set("bounds", Value::List{Value(100), Value(200), Value(30), Value(40)});
        changed_boxes.set("nodes", Value(std::move(moved)));
        expect(site::tree_hash(changed_boxes) == h) << "geometry does not change the semantic hash";
        // The compact landing places the CTAs after the visual. The same
        // heading and actions remain available through that responsive layout.
        Headless narrow{HeadlessOptions{.size = {820, 640}}};
        AppSpec spec = site::app();
        narrow.runtime().set_theme(spec.theme);
        spec.setup(narrow.runtime());
        narrow.mount("main", spec.view());
        narrow.settle(seconds(0.9));
        expect(narrow.tree().node("main/body/view/home/page/landing/intro/title")->name == "One interface. Every perspective.");
        for (const auto name : {"Explore components", "See the motion"}) {
            const auto buttons = narrow.tree().find({.under = "main/body/view/home/page/landing", .role = Role::button});
            expect(std::ranges::any_of(buttons, [name](const auto& node) { return node.name == name; })) << name;
        }
        // The Chinese page is another tree.
        expect(a.h.click("main/bar/lang"));
        a.h.settle();
        expect(site::tree_hash(a.snapshot("home")) != h) << "another language, another tree";
    };

    "the live timeline editor: an agent's edit, the person's undo, the tree that follows"_test = [] {
        Site a;
        a.inspect();
        expect(a.navigate("timeline"));
        a.h.settle();
        constexpr std::string_view editor = "main/body/view/timeline/page/editor";
        const auto before = site::tree_hash(a.snapshot("timeline"));
        auto tracks = [&] { return a.h.tree().node(editor)->value.get("tracks")->json(); };
        const std::string was = tracks();
        Invocation agent;
        agent.actor = {"agent", "test", {}};
        agent.args = Value{}.set("track", 0).set("clip", 1).set("to_track", 0).set("frame", 100);
        expect(a.h.tree().act(editor, "move", agent).exit_code().value_or(-1) == std::int64_t{0}) << "an agent moves the fight";
        a.h.settle();
        expect(site::tree_hash(a.snapshot("timeline")) != before) << "the tree follows the edit";
        expect(a.text("main/foot/proof/hash") != std::format("tree {:016x}", before)) << "and so does the footer";
        expect(a.h.tree().act(editor, "undo", agent).exit_code().value_or(-1) == std::int64_t{0}) << "undone in the editor's own history";
        a.h.settle();
        expect(tracks() == was) << "the very timeline again";
    };

    "the page's time: dragged back into a motion, the page is drawn as it was then; played backwards and forwards; live again"_test = [] {
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
            expect(area.width > 0 && area.height > 0) << "the real document is visible";
            return a.pixels_in(area);
        };
        a.h.advance(seconds(0.5));
        const double switched = a.h.runtime().now().count();
        expect(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.settle(seconds(3));
        const auto at_rest = document_pixels();
        expect(a.h.tree().node(bar) && a.h.tree().node(bar)->role == Role::slider) << "the page's time, a slider";
        expect(a.text("main/foot/clock") == "现在") << "live";
        // An agent drags the playhead into the document's actual line arrival.
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(switched + 0.15));
        expect(a.h.tree().act(bar, "set_value", agent).exit_code() == std::optional<int>(0)) << "set by an agent, as a person drags it";
        a.h.settle(seconds(2));
        const auto then = document_pixels();
        expect(then != at_rest) << std::format("the document's lines arriving, held: {}", a.text("main/foot/clock"));
        expect(a.text("main/foot/clock").starts_with("−")) << std::format("seconds before the newest: {} (value {}, target {})", a.text("main/foot/clock"), a.value(bar), switched + 0.15);
        // Live again: the page as it is.
        expect(a.h.click("main/foot/time_live"));
        a.h.settle(seconds(2));
        expect(document_pixels() == at_rest && a.text("main/foot/clock") == "现在") << "live: at rest";
        // Backwards: the playhead earlier each frame, until the strip's start.
        expect(a.h.click("main/foot/time_back"));
        const double from = a.value(bar);
        for (int i = 0; i < 20; ++i) a.h.step();
        expect(a.value(bar) < from) << "earlier each frame";
        a.h.advance(seconds(30));
        expect(a.value(bar) == a.value(bar, "min")) << "stopped at the strip's start";
        // Played forwards from there: at the end, live.
        expect(a.h.click("main/foot/time_play"));
        a.h.advance(seconds(30));
        a.h.settle(seconds(2));
        expect(a.text("main/foot/clock") == "现在") << "live at the end";
        expect(document_pixels() == at_rest) << "at rest, as it was";
    };

    "the components: curated search, live detail, reset, languages and scoped appearance"_test = [] {
        // The library is a component's page (the studio shows the components themselves): opened at the button.
        Site a("components/button");
        a.h.move({4, 796}); a.h.advance(seconds(1));
        const std::string root = "main/body/view/components/page/gallery";
        const std::string overview = root + "/overview";
        expect(eq(a.text(overview + "/results/count"), std::string("8 / 36")));
        expect(a.h.tree().node(root + "/detail/button/stage/specimen").has_value()) << "one live specimen initially";
        expect(!a.h.tree().node(root + "/detail/button/code") && !a.h.tree().node(root + "/detail/button/options")) << "details start closed";
        Invocation visitor; visitor.actor = {"agent", "visitor", {}};
        auto act = [&](const std::string& id, std::string_view action, Value args = {}) {
            visitor.args = std::move(args);
            expect(a.h.tree().act(id, action, visitor).exit_code() == std::optional<int>(0)) << id + " " + std::string(action);
            a.h.settle(); a.h.advance(seconds(0.1));
        };
        act(overview + "/filter/search", "set_value", Value{}.set("value", Value("BADGE")));
        expect(eq(a.text(overview + "/results/count"), std::string("1 / 36")));
        act(overview + "/results/r0/badge/heading/open", "activate");
        const std::string detail = root + "/detail/badge";
        expect(eq(a.h.runtime().route(), std::string("components/badge")));
        expect(a.h.tree().node(detail + "/stage/specimen").has_value());
        expect(!a.h.tree().node(detail + "/code")) << "source is optional";
        act(detail + "/panes/code", "activate");
        expect(eq(a.text(detail + "/code/scroll/source"), std::string("badge({.count = 3, .id = \"mail\", .tone = BadgeTone::danger})")));
        for (const auto& [language, expected] : {
                 std::pair{"ts", "badge({ id: \"mail\", count: 3, tone: \"danger\" })"}}) {
            act(detail + "/code/language/" + language, "activate");
            expect(eq(a.text(detail + "/code/scroll/source"), std::string(expected)));
        }
        act(detail + "/code/copy", "activate");
        expect(eq(a.h.runtime().clipboard_text(), a.text(detail + "/code/scroll/source")));
        expect(eq(a.text(detail + "/code/copied"), std::string("Expression copied")));
        act(detail + "/panes/semantics", "activate");
        const std::string semantics = a.text(detail + "/semantics/scroll/json");
        expect(semantics.contains("specimen") && semantics.contains("mail")) << "the actual specimen's semantic nodes";
        expect(eq(a.text(overview + "/results/count"), std::string("1 / 36")));
        act(overview + "/filter/search", "set_value", Value{}.set("value", Value("no-such-component")));
        expect(a.h.tree().node(overview + "/results/empty").has_value()) << "useful empty search";
        expect(a.h.tree().node(detail + "/stage/specimen").has_value()) << "an empty directory keeps the current live specimen";
        act(overview + "/filter/clear", "activate");
        act(overview + "/results/r0/button/heading/open", "activate");
        const std::string button = root + "/detail/button";
        act(button + "/stage/specimen/variants/primary", "activate");
        expect(eq(a.text(button + "/stage/specimen/variants/primary"), std::string("Saved 1")));
        act(button + "/stage/actions/reset", "activate");
        expect(eq(a.text(button + "/stage/specimen/variants/primary"), std::string("Save")));
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
        expect(a.pixels_of(button + "/stage/specimen") != shown) << "the specimen in another style";
        expect(a.pixels_in({0, 0, 1280, 56}) == chrome) << "the site's bar as it was";
        expect(tree() == plain) << "only specimen pixels change";
        act(button + "/options/disabled", "toggle");
        expect(a.h.tree().node(button + "/stage/specimen/variants/primary")->states.has(State::disabled)) << "disabled state reaches the real sample";
        visitor.args = {};
        expect(a.h.tree().act(button + "/stage/specimen/variants/primary", "activate", visitor).exit_code() != std::optional<int>(0)) << "disabled blocks real activation";
        act(button + "/stage/actions/reset", "activate");
        expect(!a.h.tree().node(button + "/stage/specimen/variants/primary")->states.has(State::disabled)) << "reset restores enabled";
        expect(a.h.tree().node(button + "/stage/specimen/variants/off")->states.has(State::disabled)) << "the always-disabled variant stays disabled";
        act(button + "/panes/semantics", "activate");
        act(button + "/stage/specimen/variants/primary", "activate");
        expect(a.text(button + "/semantics/scroll/json").contains("Saved 1")) << "semantics follows an actual specimen action";
        a.h.advance(seconds(1));
        const auto quiet = a.h.step();
        expect(quiet.demand.idle() || quiet.demand.max_hz <= 10) << "live semantics does not create a frame feedback loop";
        a.h.runtime().set_route("zh/components/text-box"); a.h.settle(); a.h.advance(seconds(0.2));
        expect(eq(a.text(root + "/detail/text_box/title"), std::string("文本框")));
        expect(eq(a.h.runtime().route(), std::string("zh/components/text-box")));
    };

    "all component addresses mount alone, come to rest and have a readable static destination"_test = [] {
        Site a("components");
        const auto html = site::static_html("en");
        for (const auto& component : site::component_catalog) {
            const auto route = site::canonical_route("components", "en", component.id);
            const auto parsed = site::parse_site_route(route);
            expect(parsed && parsed->component == component.id) << route;
            expect(html.contains("id=\"/" + route + "\"")) << route + " has static content";
            a.h.runtime().set_route(route); a.h.settle(); a.h.advance(seconds(0.5));
            const auto id = "main/body/view/components/page/gallery/detail/" + std::string(component.id);
            expect(eq(a.text(id + "/title"), std::string(component.en)));
            expect(a.h.tree().node(id + "/stage/specimen").has_value()) << route;
            const auto frame = a.h.step();
            expect(frame.demand.idle() || frame.demand.max_hz <= 10) << route + " rests";
        }
        const auto multiline = site::code_of("cpp", {"code_editor", {{"text", Value("a\nb\t\"c\"")}}});
        expect(multiline.contains("a\\nb\\t\\\"c\\\"")) << "multiline API text is escaped";
        expect(!site::parse_site_route("components/unknown"));
    };

    "component layouts: real controls fit phone, tablet and desktop in both languages"_test = [] {
        const auto dir = scratch_dir("site-catalog");
        for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) for (std::string_view language : {"en", "zh"}) {
            Headless h{HeadlessOptions{.size = {width, 844}}};
            auto spec = site::app("components", language); h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
            for (std::string_view component : {"", "button", "text_box", "data_grid", "code_editor"}) {
                h.runtime().set_route(site::canonical_route("components", language, component)); h.settle(); h.advance(seconds(0.3));
                const auto nodes = h.tree().snapshot({.root = "main/body/view/components/page"}).nodes;
                // What a sideways scroll view holds may lie beyond the page (a row swiped sideways): the scroll view fits instead.
                std::vector<std::string> scrollers;
                for (const auto& node : nodes)
                    if (const Value* room = node.value.get("max_x"); node.role == Role::scroll_view && room && room->as_double(0) > 0) scrollers.push_back(node.id + "/");
                const auto scrolled = [&](const std::string& id) { return std::ranges::any_of(scrollers, [&](const std::string& in) { return id.starts_with(in); }); };
                for (const auto& node : nodes) if ((node.role == Role::button || node.role == Role::text_box || node.role == Role::scroll_view) && node.bounds &&
                        !node.states.has(State::hidden) && !node.states.has(State::offscreen) && !scrolled(node.id))
                    expect(node.bounds->x >= -0.5f && node.bounds->right() <= width + 0.5f) << std::format("{} {} {} at {}: {} + {}", component, language, node.id, width, node.bounds->x, node.bounds->width);
                if ((width == 390 && language == "zh") || (width == 1280 && language == "en"))
                    expect(h.save_png(dir / std::format("{}-{}-{}.png", component.empty() ? "overview" : component, language, static_cast<int>(width))).has_value());
            }
        }
    };

}};
