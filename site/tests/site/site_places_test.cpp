// The site, continued (split from site_test.cpp so each program stays well inside the runner's 300 s per program):
// getting started, documents' links, the agent on the page, the pinned pixels, the
// reference run, routes, documentation, exploration, page roles, the static page and the panel beside the place.
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

static UTest cases{"site.places", [] {
    "getting started: example 01 runs beside its source, typed into and clicked"_test = [] {
        Site a;
        expect(a.navigate("hello"));
        a.h.settle(seconds(3));
        constexpr std::string_view run = "main/body/view/hello/page/example/intro/run";
        const auto field = a.h.tree().find({.under = std::string(run), .role = Role::text_box});
        expect(!field.empty()) << "example 01's field, found by its role";
        if (!field.empty()) { (void)a.h.tree().act(field.front().id, "scroll_into_view"); a.h.settle(); }
        expect(!field.empty() && a.h.click(field.front().id)) << "its field focused";
        a.h.type("Ada");
        a.h.settle();
        const auto button = a.h.tree().find({.under = std::string(run), .role = Role::button});
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        expect(!button.empty() && a.h.tree().act(button.front().id, "activate", agent).exit_code() == std::optional<int>(0)) << "its button activated by an agent";
        a.h.settle();
        bool greeted = false, counted = false;
        for (const auto& n : a.h.tree().snapshot({.root = std::string(run)}).nodes) {
            greeted = greeted || n.name == "Nice to meet you, Ada!";
            counted = counted || n.name == "Clicked once";
        }
        expect(greeted) << "the program ran: greeted";
        expect(counted) << "the program ran: counted";
        expect(a.text("main/body/view/hello/page/example/code/source").contains("export module omui.example.hello_im;")) << "its source beside it";

    };

    "a document's links: another place of the site, or outside through the host"_test = [] {
        Site a("hello");
        const std::string doc = "main/body/view/hello/page/instructions/start";
        Invocation person;
        person.args = Value{}.set("href", Value("features.md")); person.answer = "yes";
        expect(a.h.tree().act(doc, "open_link", person).exit_code() == std::optional<int>(0));
        a.h.settle(); expect(a.shown("features")); expect(a.h.runtime().opened().empty());
        expect(a.navigate("docs")); a.h.settle();
        person.args = Value{}.set("href", Value("https://github.com/openlifa/openmui"));
        expect(a.h.tree().act("main/body/view/docs/page/read/resources", "open_link", person).exit_code() == std::optional<int>(0));
        expect(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openmui"});
        expect(!a.h.runtime().open_url("javascript:alert(1)"));
        Site b("hello");
        const auto links = b.h.tree().find({.under = doc, .role = Role::link});
        const auto core = std::ranges::find_if(links, [](const auto& n) { return n.name == "core ideas"; });
        expect(core != links.end()) << "the actual guide link";
        if (core != links.end()) {
            (void)b.h.tree().act(core->id, "scroll_into_view"); b.h.settle();
            expect(b.h.click(core->id));
        }
        b.h.settle(); expect(b.shown("features"));
    };

    "an agent on the page: real requests, openas deciding — it asks first, the person answers, and only the person"_test = [] {
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
            expect(a.h.click(id)) << std::format("the task {}", task);
            a.h.advance(seconds(4));
            a.h.settle(seconds(1));
        };
        expect(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        expect(a.visible("main/body/guide")) << "the panel";
        const auto said = [&] { return a.text("main/body/guide/said/transcript"); };

        run("read");
        expect(said().find(R"(→ {"op":"snapshot","actor":"agent:guide","root":"main/body/view/home/page","depth":2})") != std::string::npos) << said();
        expect(said().find(R"(← {"kind":"result","exitCode":0})") != std::string::npos) << "read: it runs";

        run("tour");
        expect(a.text("main/bar/nav/to_docs") == "文档") << "the language switched by the agent";
        expect(a.shown("features")) << "and the features open";
        expect(a.h.click("main/bar/lang"));
        a.h.settle();

        run("edit");
        expect(a.shown("timeline")) << "the timeline";
        const Value ed = a.h.tree().node("main/body/view/timeline/page/editor")->value;
        expect(ed.get("tracks") && ed.get("tracks")->list()->front().get("clips")->list()->size() == 2) << std::format("split, then undone: {}\n{}", ed.json(), said());
        expect(said().find(R"("action":"split")") != std::string::npos && said().find(R"("op":"undo")") != std::string::npos) << "split and undo, as requests";

        // External: it asks the person first (exit code 2), and waits.
        run("link");
        expect(a.visible("main/body/guide/asking")) << "the agent asks";
        expect(said().find(R"("exitCode":2)") != std::string::npos) << "needs the person's answer";
        expect(a.h.runtime().opened().empty()) << "nothing opened yet";
        // It cannot answer for the person (S-10), whatever it claims.
        run("self");
        expect(said().find("person-only") != std::string::npos || said().find("person's own") != std::string::npos) << said();
        expect(a.visible("main/body/guide/asking")) << "still asking";
        // The person allows it once: the request again with the answer.
        expect(press("main/body/guide/asking/answer/allow_once"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        expect(a.h.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openas"}) << "opened, once";
        expect(!a.visible("main/body/guide/asking")) << "nothing asked now";
        // Once was once; for this visit is a grant.
        run("link");
        expect(a.visible("main/body/guide/asking")) << "asks again";
        expect(press("main/body/guide/asking/answer/allow"));
        a.h.advance(seconds(1));
        a.h.settle(seconds(1));
        expect(a.h.runtime().opened().size() == 2) << "opened";
        expect(a.text("main/body/guide/held/grants") == "Grants: agent:guide=external@open_link") << a.text("main/body/guide/held/grants");
        run("link");
        expect(!a.visible("main/body/guide/asking") && a.h.runtime().opened().size() == 3) << "within the grant: it runs without asking";
        expect(press("main/body/guide/held/revoke"));
        a.h.settle();
        expect(a.text("main/body/guide/held/grants") == "Grants: none");
        run("link");
        expect(a.visible("main/body/guide/asking")) << "revoked: it asks again";
        expect(press("main/body/guide/asking/answer/deny"));
        a.h.settle();
        expect(a.h.runtime().opened().size() == 3 && !a.visible("main/body/guide/asking")) << "no";
        agent_access().revoke_all();
    };

    "the release scene's screens: your agent, time, motion, speed — the agent panel from its button; the scroll the motion's time; the tier chosen the settings' own"_test = [] {
        Site a("release");
        const std::string home = "main/body/view/release/page/";
        for (const char* id : {"agent", "time", "motion", "fast"}) expect(a.h.tree().node(home + id + "/claim").has_value()) << std::format("the {} screen", id);
        // The agent screen: the panel, a prompt in it.
        expect(a.h.tree().act(home + "agent/shown/actions/copy", "activate").exit_code() == std::optional<int>(0));
        a.h.settle();
        expect(a.visible("main/body/guide") && !a.text("main/body/guide/prompt").empty()) << "the panel open, a prompt given";
        expect(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        // The motion screen: scrolled to, its morph held to the scroll.
        const NodeId shape = a.h.runtime().find(home + "motion/shown/shape");
        Invocation person;
        person.args = Value{}.set("x", 0.0).set("y", 1300.0);
        expect(a.h.tree().act("main/body/view/release", "scroll_to", person).exit_code() == std::optional<int>(0));
        a.h.settle();
        a.h.advance(seconds(0.5));  // the hold takes in the frames after the morph starts
        auto at = a.h.runtime().motion_time(shape);
        expect(at.has_value()) << "its morph held";
        const double part = at ? at->second / at->first : 0;
        person.args = Value{}.set("x", 0.0).set("y", 1700.0);
        (void)a.h.tree().act("main/body/view/release", "scroll_to", person);
        a.h.settle();
        at = a.h.runtime().motion_time(shape);
        expect(at && at->second / at->first > part) << std::format("further down, further on: {} then {}", part, at ? at->second / at->first : -1);
        // The fast screen: the tier the settings choose.
        expect(a.h.tree().act(home + "fast/shown/tier/t1", "activate").exit_code() == std::optional<int>(0));
        a.h.settle();
        expect(a.h.runtime().adaptation().effects == mlibs::pipeline::Tier::t1) << "effects as T1";
        expect(a.h.tree().node(home + "fast/shown/device")->name.find("T1") != std::string::npos) << (a.h.tree().node(home + "fast/shown/device")->name);
    };

    "your own agent: the prompt with its pairing code, copied; its actions in the log, each taken back from there; the self-check it presses"_test = [] {
        Site a("release");
        expect(a.h.click("main/bar/guide_toggle"));
        a.h.settle();
        // No host to link one here: the prompt says how instead, and nothing is copied.
        expect(a.h.click("main/body/guide/copy_prompt"));
        a.h.settle();
        expect(a.text("main/body/guide/prompt").find("omui agent") != std::string::npos && a.h.runtime().clipboard_text().empty()) << a.text("main/body/guide/prompt");
        // As the Web links one: a code, the page's agent.md.
        int ended = 0;
        a.h.runtime().set_agent_linker({[&] {
                                            a.h.runtime().agent_link_changed({"waiting", "K7Q4-42MX", {}, {}, 47110, "https://openmui.dev/agent.md"});
                                            return std::string("K7Q4-42MX");
                                        },
                                        [&] { ++ended; }});
        expect(a.h.click("main/body/guide/copy_prompt"));
        a.h.settle();
        const std::string prompt = a.text("main/body/guide/prompt");
        expect(prompt.find("K7Q4-42MX") != std::string::npos && prompt.find("https://openmui.dev/agent.md") != std::string::npos && prompt.find("port 47110") != std::string::npos) << prompt;
        expect(eq(a.h.runtime().clipboard_text(), prompt));
        expect(a.h.tree().node("main/body/guide/log")->value.get("link")->as_string() == "waiting") << "the log says it waits";
        a.h.runtime().agent_link_changed({"paired", "K7Q4-42MX", "claude-code", {}, 47110, "https://openmui.dev/agent.md"});
        a.h.settle();
        // The agent, as agent.md has it: the self-check, then the person's task (a box of the checklist).
        Invocation agent;
        agent.actor = {"agent", "claude-code", {}};
        expect(a.h.tree().act("main/body/guide/check/hello", "activate", agent).exit_code() == std::optional<int>(0));
        a.h.settle();
        expect(a.text("main/body/guide/check/hellos") == "pressed 1 time") << a.text("main/body/guide/check/hellos");
        const std::string box = "main/body/view/release/page/hero/demo/specimen/items/tests/done";
        expect(a.h.tree().act(box, "toggle", agent).exit_code() == std::optional<int>(0));
        a.h.settle();
        expect(a.h.tree().node(box)->states.has(State::checked)) << "checked by the agent";
        auto items = a.h.runtime().virtual_items(a.h.runtime().find("main/body/guide/log"));
        expect(items.size() == 2 && items[0].name == "claude-code activate …/check/hello" && items[1].name.starts_with("claude-code toggle")) << (items.empty() ? std::string("none") : items.back().name);
        // The person takes it back from the log.
        expect(std::ranges::any_of(items[1].actions, [](const spec::ActionDesc& d) { return d.name == "undo"; })) << "the last its history holds";
        Invocation person;
        expect(a.h.tree().act(items[1].id, "undo", person).exit_code() == std::optional<int>(0));
        a.h.settle();
        expect(!a.h.tree().node(box)->states.has(State::checked)) << "taken back";
        // Ending the link is the person's: the agent cannot, the person can.
        Invocation over_the_bus = agent;
        over_the_bus.remote = true;  // as the bridge brings it
        expect(a.h.tree().act("main/body/guide/disconnect", "activate", over_the_bus).exit_code() != std::optional<int>(0)) << "not the agent's to press";
        expect(a.h.click("main/body/guide/disconnect"));
        a.h.settle();
        expect(ended == 1 && a.text("main/body/guide/prompt").empty()) << "ended by the person";
    };

    "the site's pixels pinned on every platform: home, the components, the wireframe, the agent's JSON, the timeline editor, getting started, an agent asking, the settings, Chinese, the page's time held"_test = [] {
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
            expect(a.h.save_png(dir / name).has_value());
        };
        auto go = [&](std::string_view place) {
            expect(a.navigate(place));
            a.h.move({4, 796});
            a.h.advance(seconds(2));
        };
        shot("home.png");
        go("components");
        shot("components.png");
        go("home");
        a.inspect();
        expect(a.h.click("main/foot/lens_tree"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("tree.png");
        expect(a.h.click("main/foot/lens_agent"));
        a.h.advance(seconds(0.3));
        a.h.advance(seconds(0.1));
        shot("agent.png");
        expect(a.h.click("main/foot/lens_pixels"));
        go("timeline");
        shot("timeline.png");
        go("hello");
        shot("hello.png");
        // The agent asks before something external (S-11), the answer the person's alone (S-10).
        go("home");
        expect(a.h.click("main/bar/guide_toggle"));
        expect(a.h.click("main/body/guide/tasks/task_link"));
        a.h.advance(seconds(3));
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        shot("agent-asks.png");
        expect(a.h.click("main/body/guide/asking/answer/deny"));
        expect(a.h.click("main/bar/settings_toggle"));
        a.h.move({4, 796});
        a.h.advance(seconds(1));
        shot("settings.png");
        expect(a.h.click("main/bar/settings_toggle"));
        go("home");
        const double switched = a.h.runtime().now().count();
        expect(a.h.click("main/bar/lang"));
        a.h.move({4, 796});
        a.h.advance(seconds(2));
        shot("zh.png");
        // The page's time dragged back into the switch: the letters gathering again, paused.
        Invocation person;
        person.args = Value{}.set("value", Value(switched + 0.15));
        expect(a.h.tree().act("main/foot/time", "set_value", person).exit_code() == std::optional<int>(0));
        a.h.advance(seconds(1));
        shot("time.png");
        std::println("    pixels {}", all.hex());
        // Reviewed production frames after the text/contrast, sampled-layout
        // and nested default-motion corrections. Intermediate ink, bounds,
        // real input and seek regressions have separate behavioural CTS.
        // Re-pinned with M1 ⑤b: the pages show the packages' new names (omui.*).
        // Re-pinned with D3b: home shows Mui's pixels gathering on the mark.
        // Re-pinned with D3c: Mui's inks on its hull's straight sides carried
        // by the cage's edge weights (hanim fix), the home's scenes below
        // (the foot says the home tree's hash; the lenses show the tree).
        // Re-pinned with D4a/D4b: the components page opens on the wall (its search and style packs).
        // Re-pinned with D6: the guide offers the permission demonstration (the agent-asks picture).
        // Re-pinned with the lead page (Part 2 §2.3): the docs' directory lists "Lead" after the practical guide.
        // Re-pinned with D-138: the docs say C++, TypeScript and view documents (features §4, the practical
        // guide's table), the interface page compares C++ and TypeScript, the combobox sample picks how a view is written.
        expect(eq(all.hex(), std::string("723756eefb1015ed")));
    };

    "the reference run: each sampled frame's pixels hashed as a browser hashes them, pinned (reference/ci.frames.xdef.json)"_test = [] {
        site::ReferenceReplay replay;
        std::vector<std::string> got;
        for (std::int32_t f = 0; f < site::reference_frames(); ++f) got.push_back(std::format("{:016x}", replay.hash(f)));
        expect(std::format("{:016x}", replay.hash(17)) == got[17]) << "a frame played to again, from the start: the same";
        expect(std::set(got.begin(), got.end()).size() >= 4) << "the run moves: its frames differ";
        const auto pinned = site::reference_pinned();
        bool same = pinned.size() == got.size();
        for (std::size_t i = 0; same && i < got.size(); ++i) same = std::format("{:016x}", pinned[i]) == got[i];
        if (!same) {  // the table as this run makes it, to look at and pin
            std::string json = "{\n  \"about\": \"The site's reference run: each sampled frame's pixel hash as CI computed it (site_test writes and checks it; the page compares a browser's own with it).\",\n  \"fps\": 10,\n  \"size\": [1280, 800],\n  \"frames\": [";
            for (std::size_t i = 0; i < got.size(); ++i) json += std::format("{}\n    \"{}\"", i ? "," : "", got[i]);
            json += "\n  ]\n}\n";
            const auto out = scratch_dir("site-reference") / "ci.frames.xdef.json";
            std::ofstream(out, std::ios::binary) << json;
            std::println("    the reference run's table: {}", out.string());
        }
        expect(same) << std::format("{} frames pinned, {} made; first made {}", pinned.size(), got.size(), got.empty() ? "" : got[0]);
        // A few of its moments, to look at: the flight, the switch, the lens fading in.
        const auto dir = scratch_dir("site-reference-frames");
        for (const std::int32_t f : {9, 30, 48}) {
            (void)replay.hash(f);
            if (auto png = replay.png()) std::ofstream(dir / std::format("frame-{:02}.png", f), std::ios::binary).write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
        }
    };

    "the check against CI in the page: the reference run's frame drawn here, its hash beside CI's, the same"_test = [] {
        Site a;
        a.inspect();
        expect(a.h.click("main/foot/ci_check"));
        a.h.advance(seconds(0.5));
        expect(a.visible("main/body/check")) << "the panel";
        Invocation agent;
        agent.actor = {"agent", "visitor", {}};
        agent.args = Value{}.set("value", Value(9.0));
        expect(a.h.tree().act("main/body/check/ref_frame", "set_value", agent).exit_code() == std::optional<int>(0)) << "a frame chosen";
        a.h.advance(seconds(0.5));
        site::ReferenceReplay own;
        const std::string want = std::format("{:016x}", own.hash(9));
        expect(a.text("main/body/check/mine").ends_with(want)) << a.text("main/body/check/mine");
        expect(a.text("main/body/check/ci").ends_with(want) && a.text("main/body/check/verdict").starts_with("✓")) << a.text("main/body/check/verdict");
        (void)a.h.save_png(scratch_dir("site-check") / "panel.png");
    };

    "the route catalog: canonical addresses, existing links, languages and missing routes"_test = [] {
        for (const auto& p : site::places()) for (std::string_view language : {"en", "zh"}) {
            for (const std::string address : {site::canonical_route(p.id, language), std::string(language) + "/" + std::string(p.id)}) {
                // A place's id is an address too, unless another place lives there (`motion` is the motion page; its specimens are `docs/motion`).
                if (address != site::canonical_route(p.id, language) &&
                    std::ranges::any_of(site::places(), [&](const site::Place& other) { return other.id != p.id && other.route == p.id; }))
                    continue;
                const auto parsed = site::parse_site_route("#/" + address + "/");
                expect(parsed && parsed->place == p.id && parsed->language == language) << address;
            }
        }
        expect(!site::parse_site_route("docs/not-a-guide"));
        Site a("#/zh/docs/features");
        expect(a.shown("features") && a.text("main/bar/nav/to_docs") == "文档");
        a.h.runtime().set_route("zh/brand"); a.h.settle(); a.h.advance(seconds(0.1));
        expect(a.shown("brand")) << "old brand links remain valid";
        expect(eq(a.h.runtime().route(), std::string("zh/brand")));
        a.h.runtime().set_route("zh/missing"); a.h.settle(); a.h.advance(seconds(0.1));
        expect(a.shown("not_found")) << "unknown routes have an error page";
        expect(a.snapshot("not_found").json().contains("zh/missing"));
        expect(a.h.click("main/bar/lang")); a.h.settle();
        expect(eq(a.h.runtime().route(), std::string("missing")));
        expect(a.snapshot("not_found").json().contains("This page isn't here."));
        expect(a.h.click("main/bar/lang")); a.h.settle();
        expect(eq(a.h.runtime().route(), std::string("zh/missing")));
        expect(a.navigate("home")); a.h.settle(); expect(a.shown("home"));
    };

    "documentation: searchable guides, selected navigation, retained query and separate reading"_test = [] {
        Site a("docs");
        const std::string read = "main/body/view/docs/page/read/";
        expect(a.h.tree().node(read + "results/hello/open").has_value());
        expect(a.h.tree().node(read + "results/features/open").has_value());
        expect(a.h.tree().node("main/bar/nav/to_docs")->states.has(State::selected));
        Invocation input; input.args = Value{}.set("value", "omui new");
        expect(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.tree().node(read + "results/hello/open").has_value()) << "search includes guide content";
        input.args = Value{}.set("value", "no-such-guide-12345");
        expect(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.tree().node(read + "results/empty").has_value());
        expect(!a.h.tree().node(read + "results/hello/open"));
        expect(a.navigate("explore")); a.h.settle(); expect(a.navigate("docs")); a.h.settle();
        expect(a.h.tree().node(read + "results/empty").has_value()) << "query survives leaving the page";
        input.args = Value{}.set("value", "");
        expect(a.h.tree().act(read + "search", "set_value", input).exit_code() == std::optional<int>(0)); a.h.settle();
        expect(a.h.click(read + "results/features/open")); a.h.settle();
        expect(a.shown("features") && a.h.tree().node("main/body/view/features/page/directory/to_features")->states.has(State::selected));
        expect(!a.h.tree().node("main/body/view/home/page/doc"));
        expect(a.h.save_png(scratch_dir("site-structure") / "docs-reading.png").has_value());
    };

    "exploration: each exhibit is a working program, the mark returns home, tools are opt-in"_test = [] {
        Site a("explore");
        expect(!a.h.tree().node("main/foot/lens_tree"));
        expect(a.h.save_png(scratch_dir("site-structure") / "explore.png").has_value());
        expect(a.navigate("brand")); a.h.settle();
        expect(a.h.tree().node("main/body/view/brand/page/identity/controls/assemble").has_value());
        expect(a.h.tree().node("main/bar/nav/to_explore")->states.has(State::selected));
        expect(a.navigate("home")); a.h.settle();
        expect(a.h.tree().node("main/body/view/home/page/landing").has_value()) << "the mark returns to the simplified home";
        expect(a.navigate("release")); a.h.settle();
        Invocation scroll; scroll.args = Value{}.set("x", 0.0).set("y", 400.0);
        expect(a.h.tree().act("main/body/view/release", "scroll_to", scroll).exit_code() == std::optional<int>(0)); a.h.settle();
        const float before = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/release/page/hero")).y;
        expect(a.navigate("docs")); a.h.settle(); expect(a.navigate("release")); a.h.settle();
        const float after = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/release/page/hero")).y;
        expect(std::abs(before - after) < 1) << std::format("page scroll restored: {} vs {}", before, after);
        a.inspect(); expect(a.h.click("main/foot/lens_tree")); a.h.settle();
        expect(a.h.click("main/foot/tools_toggle")); a.h.settle();
        expect(!a.h.tree().node("main/foot/lens_tree") && !a.visible("main/body/view/wireframe"));
    };

    "the new page roles: desktop and phone, English and Chinese, controls stay within the viewport"_test = [] {
        const auto dir = scratch_dir("site-structure");
        for (const bool phone : {false, true}) for (std::string_view lang : {"en", "zh"})
            for (std::string_view place : {"home", "release", "docs", "explore", "hello", "features"}) {
                const Size size = phone ? Size{390, 844} : Size{1280, 800};
                Headless h{HeadlessOptions{.size = size}};
                AppSpec spec = site::app(place, lang); h.runtime().set_theme(spec.theme); spec.setup(h.runtime()); h.mount("main", spec.view()); h.settle();
                const std::string root = std::format("main/body/view/{}/page", place);
                expect(h.tree().node(root).has_value()) << std::format("{} {} {}", place, lang, phone);
                for (const auto& n : h.tree().snapshot({.root = root}).nodes) {
                    if ((n.role == Role::button || n.role == Role::text_box) && n.bounds && !n.states.has(State::hidden) && !n.states.has(State::offscreen))
                        expect(n.bounds->x >= -0.5f && n.bounds->x + n.bounds->width <= size.width + 0.5f) << std::format("{} {} {} inside: {} + {}", place, lang, n.id, n.bounds->x, n.bounds->width);
                }
                expect(h.save_png(dir / std::format("{}-{}-{}.png", place, lang, phone ? "phone" : "desktop")).has_value());
            }
    };

    "the static page: every place's HTML, md4c-html's bytes, in a page that hides it once the program runs"_test = [] {
        for (std::string_view lang : {"en", "zh"}) {
            const std::string html = site::static_html(lang);
            expect(html.starts_with("<main id=\"omui-static\"")) << lang;
            const auto skip = html.find("<a class=\"skip\" href=\"#/"), nav = html.find("<nav aria-label=");
            expect(skip != std::string::npos && skip < nav) << std::format("{}: a skip link before the places", lang);
            for (const site::Place& p : site::places()) expect(html.find(std::format("<section id=\"/{}\"", p.id == "home" ? (lang == "zh" ? "zh/home" : "home") : site::canonical_route(p.id, lang))) != std::string::npos) << std::format("{} {}", lang, p.id);
            expect(html.find(mlibs::markdown::to_html(mlibs::markdown::parse(site::document("features", lang)))) != std::string::npos) << "the features as md4c-html writes them";
        }
        const std::string en = site::static_html("en");
        expect(en.find("aria-label=\"One interface. Every perspective.\"") != std::string::npos && en.find("One interface.<br>Every perspective.") != std::string::npos) << "the thesis and deliberate line break, readable without JavaScript";
        // The site's own part pinned (the docs change with the framework; they are held to md4c-html's bytes above).
        auto own = [](const std::string& html) { return html.substr(0, html.find("<section id=\"/", html.find("<section id=\"/") + 1)); };
        std::println("    static en {:016x} zh {:016x}", fnv(own(en)), fnv(own(site::static_html("zh"))));
        expect(eq(std::format("{:016x}", fnv(own(en))), std::string("079033371bb4d55a")));
        expect(eq(std::format("{:016x}", fnv(own(site::static_html("zh")))), std::string("2da35a9ed4fbf2db")));

        const auto out = site::inject(page, en, "zh");
        expect(out.has_value()) << "dist-toy's page";
        const std::string& p = *out;
        expect(p.find("<html lang=\"zh-CN\">") != std::string::npos) << "the page's language";
        const auto main = p.find("<main id=\"omui-static\""), canvas = p.find("<canvas id=\"canvas\""), hide = p.find("window.omuiHasPresented"),
                   loader = p.find("<script defer src=\"omui-site.js\">");
        expect(main < canvas) << "the static page before the canvas";
        expect(hide != std::string::npos && hide < loader) << "hidden by a script before the program's";
        expect(p.find("#omui-static{") < p.find("</head>")) << "its style in the head";
        expect(p.find("@font-face{font-family:'Inter';src:url(assets/4304c4ca3090.ttf)") != std::string::npos) << "in the program's own font";
        const auto no_script = p.find("<noscript><style>"), no_script_end = p.find("</style></noscript>", no_script);
        const auto font_face = p.find("@font-face");
        expect(no_script < font_face && font_face < no_script_end) << "URL fonts are only a no-JavaScript fallback; active JavaScript shares managed bytes";
        expect(p.contains("id=\"omui-static-fonts\" type=\"application/json\"")) << "static font consumers get inert descriptors before bootstrap";
        expect(p.contains("\"name\":\"Inter.ttf\"") && p.contains("\"when\":\"body\"")) << "the body face is declared by the same selected font list";
        expect(p.contains("#omui-static>nav[data-default]{display:flex}")) << "the selected static navigation keeps its layout";
        std::string initial_page(page);
        const auto font_url = initial_page.find("\"assets/4304c4ca3090.ttf\"");
        initial_page.replace(font_url, std::string_view("\"assets/4304c4ca3090.ttf\"").size(), "\"assets/full.ttf\",[],\"assets/initial.ttf\"");
        const auto initial_out = site::inject(initial_page, en, "en");
        expect(initial_out && initial_out->contains("font-family:'Inter';src:url(assets/initial.ttf)")) << "the readable opening uses its initial face instead of a second full-face request";
        expect(!site::inject("<html><body>nothing</body></html>", en, "en").has_value()) << "a page dist-toy did not write";
        expect(!site::inject(p, en, "en").has_value()) << "a page already pre-rendered";
        const auto openings = site::inject(page, en, "en", R"({"version":1,"previews":[],"note":"</script><script>bad()</script>"})");
        expect(openings && openings->contains("id=\"omui-first-frames\"")) << "build-time asset references precede the bootstrap";
        expect(openings && !openings->contains("<script>bad()")) << "asset catalogue data cannot close its script element";
        expect(!site::inject(page, en, "en", "not json")) << "malformed opening metadata is rejected";
        expect(!site::inject(page, en, "en", R"({"version":2,"previews":[]})")) << "unknown opening metadata version is rejected";
    };

    "on a phone: the agent's tour — the panel covers the place, the tour puts it away and the place is read"_test = [] {
        auto h = std::make_unique<Headless>(HeadlessOptions{.size = {390, 844}});
        AppSpec spec = site::app("home", "en");
        h->runtime().set_theme(spec.theme);
        spec.setup(h->runtime());
        h->mount("main", spec.view());
        h->settle();
        expect(h->click("main/bar/guide_toggle")) << "the agent's panel";
        h->settle();
        expect(h->click("main/body/guide/tasks/task_tour")) << "the tour";
        h->advance(seconds(6));
        h->settle(seconds(1));
        const auto doc = h->tree().node("main/body/view/features/page/read/doc");
        expect(doc && !doc->states.has(State::offscreen)) << "the features read, in view";
        const Rect view = h->runtime().screen_bounds(h->runtime().find("main/body/view"));
        expect(view.width == 390) << std::format("the view the whole width again ({})", view.width);
        const Rect live = h->runtime().screen_bounds(h->runtime().find("main/foot/tools_toggle"));
        expect(live.height < 60 && live.x + live.width <= 390) << std::format("the foot's Live on one line, inside ({} high, to {})", live.height, live.x + live.width);
    };

    "a panel opened beside the place: the place is laid out again to the room left"_test = [] {
        Site a;
        // Landmarks a screen reader jumps between: the places' navigation, the place itself.
        expect(a.h.tree().node("main/bar/nav") && a.h.tree().node("main/bar/nav")->role == Role::navigation) << "the navigation landmark";
        expect(a.h.tree().node("main/bar/mark")->states.has(State::selected) && !a.h.tree().node("main/bar/nav/to_docs")->states.has(State::selected)) << "where you are: the current place selected";
        expect(a.h.tree().node("main/body/view") && a.h.tree().node("main/body/view")->role == Role::main) << "the main landmark";
        const Rect before = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view"));
        expect(a.h.click("main/bar/guide_toggle"));
        a.h.advance(seconds(1));
        const Rect view = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view"));
        expect(view.width < before.width) << std::format("the view narrower: {} → {}", before.width, view.width);
        const Rect page = a.h.runtime().screen_bounds(a.h.runtime().find("main/body/view/home/page"));
        expect(page.x >= view.x - 1 && page.x + page.width <= view.x + view.width + 1) << std::format("the page within the view: page {} + {}, view {} + {}", page.x, page.width, view.x, view.width);
        // Nothing of the page's content under the panel.
        std::string under;
        for (const auto& n : a.h.tree().snapshot({.root = "main/body/view/home/page"}).nodes)
            if (n.bounds && n.bounds->x + n.bounds->width > view.x + view.width + 1 && !n.states.has(State::offscreen) && !n.states.has(State::hidden))
                under += std::format("{} ({}) ", n.id, n.bounds->x + n.bounds->width);
        expect(under.empty()) << "nothing under the panel: " + under;
        (void)a.h.save_png(scratch_dir("site-panel") / "home-with-panel.png");
    };
}};
