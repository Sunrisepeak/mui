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
        h.mount("main", spec.view());
        h.settle();
    }
    Value snapshot(std::string_view place) { return h.tree().snapshot({.root = std::format("main/body/view/{}", place)}).to_value(); }
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
    bool visible(std::string_view id) {
        const auto n = h.tree().node(id);
        return n && !n->states.has(State::hidden);
    }
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
            if (p.id == "timeline") {
                check(site::document(p.id, "en").empty(), "the timeline is a live work, not a document");
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
        check(a.visible("main/body/view/home"), "home first");
        check(!a.visible("main/body/view/features"), "one place at a time");
        for (const site::Place& p : site::places()) {
            check(a.h.click(std::format("main/body/nav/to_{}", p.id)), std::format("the nav's {}", p.id));
            a.h.settle();
            for (const site::Place& q : site::places())
                check(a.visible(std::format("main/body/view/{}", q.id)) == (p.id == q.id), std::format("{} shown: {}", p.id, q.id));
            const std::size_t nodes = a.snapshot(p.id).get("nodes")->list()->size();
            check(nodes >= (p.id == "timeline" ? 3u : 5u), std::format("{}: its tree ({} nodes)", p.id, nodes));
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
        const auto direct = zh.pixels();
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(0.12));
        const auto moving = a.pixels();
        check(moving != direct, "under way: the letters still gathering");
        check(site::tree_hash(a.snapshot("home")) == site::tree_hash(zh.snapshot("home")), "the tree is already the other language's: the motion is only paint");
        a.h.settle(seconds(3));
        check(a.pixels() == direct, "at rest: the very pixels of the page opened in Chinese");
    });

    s.test("the lens: pixels, the tree's wireframe, an agent's JSON, a test's assertions — of the same nodes", [] {
        Site a;
        const auto plain = a.pixels();
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
        check(json.find("\"id\":\"main/body/view/home") != std::string::npos || json.find("\"id\": \"main/body/view/home") != std::string::npos,
              "the nodes as an agent reads them");
        for (const Value& n : *home.get("nodes")->list()) check(json.find(n.get("id")->json()) != std::string::npos, std::format("{} in the agent's JSON", n.get("id")->json()));

        check(a.h.click("main/bar/lens_test"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        const std::string lines = a.text("main/body/view/test/test_text");
        check(lines.find("check_eq(node(\"main/body/view/home") != std::string::npos, "assertions of the same nodes");
        check(lines.find("check_eq(node(\"main/body/view/home/home_doc/b0\").role, \"heading\");") != std::string::npos, "the title's role asserted");

        check(a.h.click("main/bar/lens_pixels"));
        a.h.move({4, 796});  // the pointer off the buttons (no hover)
        a.h.settle(seconds(1));
        check(a.pixels() == plain, "back to the pixels, the same pixels");
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
        check(site::tree_hash(narrow.tree().snapshot({.root = "main/body/view/home"}).to_value()) == h, "the same tree, another window");
        // The Chinese page is another tree.
        check(a.h.click("main/bar/lang"));
        a.h.settle();
        check(site::tree_hash(a.snapshot("home")) != h, "another language, another tree");
    });

    s.test("the live timeline editor: an agent's edit, the person's undo, the tree that follows", [] {
        Site a;
        check(a.h.click("main/body/nav/to_timeline"));
        a.h.settle();
        constexpr std::string_view editor = "main/body/view/timeline/editor";
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

    s.test("a document's links: another place of the site, or outside through the host", [] {
        Site a;
        Invocation person;  // the person at the machine (not over the bus)
        person.args = Value{}.set("href", Value("features.md"));
        person.answer = "yes";
        check(a.h.tree().act("main/body/view/home/home_doc", "open_link", person).exit_code() == std::optional<int>(0), "a link to the features");
        a.h.settle();
        check(a.visible("main/body/view/features"), "the features shown");
        check(a.h.runtime().opened().empty(), "nothing opened outside");
        check(a.h.click("main/body/nav/to_home"));
        a.h.settle();
        person.args = Value{}.set("href", Value("https://github.com/openlifa/openmui"));
        check(a.h.tree().act("main/body/view/home/home_doc", "open_link", person).exit_code() == std::optional<int>(0), "the spec's repository");
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
        check(said().find(R"(→ {"op":"snapshot","actor":"agent:guide","root":"main/body/view/home","depth":2})") != std::string::npos, said());
        check(said().find(R"(← {"kind":"result","exitCode":0})") != std::string::npos, "read: it runs");

        run("tour");
        check(a.text("main/body/nav/to_features") == "特性", "the language switched by the agent");
        check(a.visible("main/body/view/features"), "and the features open");
        check(a.h.click("main/bar/lang"));
        a.h.settle();

        run("edit");
        check(a.visible("main/body/view/timeline"), "the timeline");
        const Value ed = a.h.tree().node("main/body/view/timeline/editor")->value;
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

    s.test("the site's pixels pinned on every platform: home, the wireframe, the agent's JSON, the timeline, an agent asking, Chinese", [] {
        Site a;
        const auto dir = scratch_dir("site");
        Hash all;
        all.u64(a.pixels());
        check(a.h.save_png(dir / "home.png").has_value());
        check(a.h.click("main/bar/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        all.u64(a.pixels());
        check(a.h.save_png(dir / "tree.png").has_value());
        check(a.h.click("main/bar/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.settle(seconds(0.1));
        all.u64(a.pixels());
        check(a.h.save_png(dir / "agent.png").has_value());
        check(a.h.click("main/bar/lens_pixels"));
        check(a.h.click("main/body/nav/to_timeline"));
        a.h.move({4, 796});
        a.h.settle();
        all.u64(a.pixels());
        check(a.h.save_png(dir / "timeline.png").has_value());
        // The agent asks before something external (S-11), the answer the person's alone (S-10).
        check(a.h.click("main/body/nav/to_home"));
        check(a.h.click("main/bar/guide_toggle"));
        check(a.h.click("main/body/guide/tasks/task_link"));
        a.h.advance(seconds(3));
        a.h.move({4, 796});
        a.h.settle(seconds(1));
        all.u64(a.pixels());
        check(a.h.save_png(dir / "agent-asks.png").has_value());
        check(a.h.click("main/body/guide/asking/answer/deny"));
        check(a.h.click("main/bar/guide_toggle"));
        check(a.h.click("main/body/nav/to_home"));
        check(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.settle();
        all.u64(a.pixels());
        check(a.h.save_png(dir / "zh.png").has_value());
        std::println("    pixels {}", all.hex());
        check_eq(all.hex(), std::string("e3b5d795708d59e9"));
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
        // The site's own part pinned (the docs change with the framework; they are held to md4c-html's bytes above).
        auto own = [](const std::string& html) { return html.substr(0, html.find("<section id=\"/", html.find("<section id=\"/") + 1)); };
        std::println("    static en {:016x} zh {:016x}", fnv(own(en)), fnv(own(site::static_html("zh"))));
        check_eq(std::format("{:016x}", fnv(own(en))), std::string("b9a7917262a78ce0"));
        check_eq(std::format("{:016x}", fnv(own(site::static_html("zh")))), std::string("32c1727fd259c0e2"));

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
