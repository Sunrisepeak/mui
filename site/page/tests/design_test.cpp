// The site's one page, designed live (G5; the site's design §8): from what `omui new` leaves, through the designer's
// verbs — the same an agent reaches over MCP (`design_*`) — every step a commit of the design's history:
//
//   1. the template, as `omui new` writes it (design/start.ts);
//   2. the agent drafts: the mark, the sentence under it, the Live Design key, the install's two tabs;
//   3. Mui: the four states of one picture, her head in the circle when an agent connects;
//   4. the person takes over: the sentence in their words, the install given more room;
//   5. the agent reads the facts back (the history says who changed what) against what the page must say;
//   6. a narrower install tried on a branch from before the person's touch, and left;
//   7. at rest: the key plays this very design in place.
//
// It ends as app/main.ts is, and the design ships as design/main.replay.xdef.json, which the page plays in itself
// (generate.replay, omui.config.ts names it); OMUI_EXAMPLE_WRITE=1 writes the two instead of comparing them, for a
// change of the design that is meant.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui;
import omui.player;
import omui.entry.player;
import omui.designer;
import omui.lang.js;
import omui.lang.ts;
import mlibs.agent;

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// What the design came to, against the file the page keeps (or written there when asked).
void pinned(const std::filesystem::path& kept, const std::string& made) {
    if (mcpplibs::os::env::get("OMUI_EXAMPLE_WRITE") == "1") {
        std::ofstream(kept, std::ios::binary) << made;
        return;
    }
    expect(file_text(kept) == made) << kept.generic_string() << " is not what the design came to (OMUI_EXAMPLE_WRITE=1 writes it, for a change that is meant)";
}

// `text` with `from` replaced by `to`, which must be there once.
std::string replaced(std::string text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    expect(at != std::string::npos && text.find(from, at + 1) == std::string::npos) << "once in the page: " << from;
    if (at != std::string::npos) text.replace(at, from.size(), to);
    return text;
}

// The mark's paths, as the agent first writes them: a plain circle around the word.
constexpr std::string_view mark_paths = R"ts(const ring = "M50 10A42 42 0 1 1 50 94A42 42 0 1 1 50 10Z";
const letters = "M24 64L24 44M24 50C24 41 35 41 35 50L35 64M35 50C35 41 46 41 46 50L46 64M55 44L55 56C55 66 69 66 69 56L69 44M78 44L78 64";
const dot = "M75.5 35A2.5 2.5 0 1 0 80.5 35A2.5 2.5 0 1 0 75.5 35Z";
const ink = "#1e1b4b";
const side = 240;
)ts";

constexpr std::string_view mark_view = R"ts(  stack({ id: "mark", width: side + 20, height: side + 20, alignItems: "center", justify: "center" }, [
    path({ id: "o", d: ring, stroke: ink, strokeWidth: 6, width: side, height: side, label: "omui" }),
    path({ id: "word", d: letters, stroke: ink, strokeWidth: 6, width: side, height: side }),
    path({ id: "dot", d: dot, fill: ink, width: side, height: side }),
  ]),
)ts";

constexpr std::string_view draft_sentence = "A UI framework in which one semantic tree drives pixels, motion, accessibility, agents and tests.";
constexpr std::string_view sentence = "A UI framework where one semantic tree drives the pixels, the motion, accessibility, agents and tests.";

// The agent's drafts, each the whole page so far.
std::string drafted(std::string_view imports, std::string_view more, std::string_view parts) {
    return std::format(R"ts(// omui's site, one page: the mark, one sentence, the Live Design key and the install.
import {{ {} }} from "openmui";

{}{}
mount("main", column({{ width: "100%", height: "100%", alignItems: "center", justify: "center", gap: 18, padding: 32, background: "#faf9f6" }}, [
{}]));
)ts",
                       imports, mark_paths, more, parts);
}

std::string with_sentence() {
    return std::format(R"ts(  text({{ id: "line", role: "heading", level: 1, style: "title", align: "center", wrap: true, maxWidth: 760, color: ink,
         text: "{}" }}),
)ts",
                       draft_sentence);
}

constexpr std::string_view key = R"ts(  button({ id: "live", variant: "secondary", text: "Live Design" }),
)ts";

constexpr std::string_view install_state = R"ts(const how = signal("agent", "page.install");
const prompt = "Install omui (after the release; until then build it from GitHub), run `omui connect 7KQ2-M9`, then call `describe` and follow it.";
const commands = "# after the release; until then build it from GitHub\nomui connect 7KQ2-M9";
)ts";

constexpr std::string_view install_view = R"ts(  column({ id: "install", label: "Install", gap: 12, width: "100%", maxWidth: 640, padding: 16, radius: 18, background: "#ffffff", shadow: "lg" }, [
    row({ justify: "space_between", alignItems: "center" }, [
      segmented({ id: "how", options: ["agent", "command"], labels: ["Agent", "Command"], value: how, label: "How to install" }),
      button({ id: "copy", variant: "primary", text: "Copy", onPress: () => copy(how.get() === "agent" ? prompt : commands) }),
    ]),
    text({ id: "words", style: "code", wrap: true, color: ink, text: select(eq(how, "agent"), prompt, commands) }),
  ]),
)ts";

}  // namespace

static UTest cases{"site.page.design", [] {
    register_base_components();

    "the page designed live from omui new: the agent's drafts, Mui, the person's touch, the facts, a branch left, and the design it ships"_test = [] {
        // 1. The application as `omui new` leaves it, in a scratch copy (the replay is written beside it).
        const std::string start = file_text(here() / "design" / "start.ts");
        std::string from_template = file_text(here().parent_path().parent_path() / "modules/projects/omui/sdk/templates/app-ts/app/main.ts.in");
        for (std::size_t at = from_template.find("{{project.name}}"); at != std::string::npos; at = from_template.find("{{project.name}}", at))
            from_template.replace(at, std::string_view("{{project.name}}").size(), "site");
        expect(start == from_template) << "design/start.ts is what omui new writes";
        const std::string page = file_text(here() / "app" / "main.ts");
        const auto dir = std::filesystem::temp_directory_path() / "omui-site-page-design";
        std::error_code error;
        std::filesystem::remove_all(dir, error);
        std::filesystem::create_directories(dir / "app", error);
        std::filesystem::copy_file(here() / "mcpp.toml", dir / "mcpp.toml", error);
        std::ofstream(dir / "app" / "main.ts", std::ios::binary) << start;
        auto manifest = read_manifest(dir / "mcpp.toml");
        expect(fatal(manifest.has_value())) << (manifest ? "" : manifest.error());

        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = manifest->size, .app = manifest->id});
        player::Player app(h.runtime(), *manifest, {lang::js::typescript()}, {.design = true, .twin = twins(*manifest)});
        expect(fatal(app.start().has_value()));
        const auto settle = [&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!app.idle() && std::chrono::steady_clock::now() < deadline) {
                (void)h.step();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            h.settle();
        };
        settle();
        std::vector<double> took;  // each edit's time, in ms
        const auto done = [&](std::string_view verb, Value args = Value(Value::Map{}), Actor actor = {"agent", "designer", {}}) {
            Invocation call;
            call.args = std::move(args);
            call.actor = std::move(actor);
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            settle();
            Value data = answer.data_value() ? *answer.data_value() : Value{answer.ndjson()};
            if (const Value* ms = data.get("took_ms"); ms && ms->is_number()) took.push_back(ms->as_double());
            expect(eq(answer.exit_code().value_or(-1), 0)) << verb << ": " << data.json();
            return data;
        };
        const auto edit = [&](std::string text, std::string_view say) {
            (void)done("design.edit", Value{}.set("path", "app/main.ts").set("text", std::move(text)).set("say", say));
        };
        // A node by its own id, wherever it sits (an unnamed container's place is in the path: main/column:0/line).
        const auto path_of = [&](std::string_view id) -> std::string {
            for (const auto& node : h.tree().snapshot().nodes)
                if (node.id.ends_with(std::format("/{}", id))) return node.id;
            return std::format("<no {}>", id);
        };
        const auto shown = [&](std::string_view id) { return !path_of(id).starts_with("<no"); };

        // 2. The agent drafts the page, a part at a time.
        (void)done("design.begin", Value{}.set("intent", "omui's site: one page, the mark, one sentence, Live Design and the install"));
        edit(drafted("stack, column, path, mount", "", mark_view), "the mark: a circle around the word");
        expect(shown("o") && shown("word")) << "the mark";
        edit(drafted("stack, column, text, path, mount", "", std::format("{}{}", mark_view, with_sentence())), "the sentence under it, the page's one heading");
        edit(drafted("stack, column, text, button, path, mount", "", std::format("{}{}{}", mark_view, key, with_sentence())), "the Live Design key between them");
        edit(drafted("signal, eq, select, stack, column, row, text, button, segmented, path, mount, copy", install_state,
                     std::format("{}{}{}{}", mark_view, key, with_sentence(), install_view)),
             "the install: an agent's one sentence or two commands, a tab each, and Copy");
        expect(shown("install") && shown("copy")) << "the install";

        // 3. Mui: the page as it ships, but for the person's touch to come and the key's last part.
        const std::string_view key_plays = R"ts(const live = button({
  id: "live", variant: "secondary", text: select(designing, "Pause", "Live Design"),
  onPress: () => {
    try { invoke("generate.replay", { play: true }); } catch { state.set(state.get() === "design" ? "rest" : "design"); }
  },
});)ts";
        const std::string_view key_still = R"ts(const live = button({ id: "live", variant: "secondary", text: select(designing, "Pause", "Live Design") });)ts";
        std::string states = replaced(page, sentence, draft_sentence);
        states = replaced(std::move(states), R"(maxWidth: 640, padding: 18,)", R"(maxWidth: 640, padding: 16,)");
        states = replaced(std::move(states), key_plays, key_still);
        edit(states, "Mui: four states of one picture, her head in the circle when an agent connects");
        expect(h.tree().find({.role = Role::button, .name = "Copy"}).size() == 1) << "at rest";

        // 4. The person takes over: the sentence in their words, the install given more room.
        (void)done("design.handover");
        const Actor person{"person", "maintainer", {}};
        (void)done("design.set", Value{}.set("node", path_of("line")).set("props", Value{}.set("text", std::string(sentence))).set("say", "the sentence, in the person's words"),
                   person);
        (void)done("design.set", Value{}.set("node", path_of("install")).set("props", Value{}.set("padding", 18)).set("say", "more room around the install"), person);
        const auto heading = h.tree().find({.role = Role::heading});
        expect(heading.size() == 1 && heading.front().name == sentence) << "the person's sentence";

        // 5. The agent reads the facts back: who changed what (the history), and what the page must say.
        const Value history = done("design.history");
        expect(history.json().contains("person:maintainer") && history.json().contains("the sentence, in the person's words")) << history.json();
        (void)done("design.expect", Value{}.set("say", "one heading, the sentence").set("query", Value{}.set("role", "heading").set("name", std::string(sentence))));
        (void)done("design.expect", Value{}.set("say", "the install copies").set("query", Value{}.set("id", path_of("copy"))));
        const Value facts = done("design.check");
        expect(facts.get("held") && facts.get("held")->as_int() == 2) << facts.json();

        // 6. A narrower install, tried on a branch from before the person's touch, and left.
        (void)done("design.history.branch", Value{}.set("name", "narrow").set("at", "HEAD~2"));
        (void)done("design.history.switch", Value{}.set("to", "narrow"));
        (void)done("design.set", Value{}.set("node", path_of("install")).set("props", Value{}.set("maxWidth", 520)).set("say", "a narrower install"));
        (void)done("design.history.switch", Value{}.set("to", "main"));
        expect(h.tree().find({.role = Role::heading}).front().name == sentence) << "back on main, the person's sentence";

        // 7. At rest: the key plays this very design. Written to the file: the TypeScript is the one source.
        edit(page, "the Live Design key plays this design in place");
        (void)done("design.commit");
        const std::string final_text = file_text(dir / "app" / "main.ts");
        expect(final_text == page) << "what the design came to is the page";
        auto compiled = mlibs::ts::compile(dir / "app" / "main.ts");
        expect(compiled.has_value()) << (compiled ? "" : compiled.error().text());

        // The design the page ships, from what omui new wrote to the page it is.
        const Value exported = done("design.history.export", Value{}.set("out", (dir / "main.replay.xdef.json").generic_string()));
        const std::string replay_text = file_text(dir / "main.replay.xdef.json");
        const auto replay = mlibs::agent::read_replay(replay_text, "main.replay.xdef.json");
        expect(fatal(replay.has_value())) << (replay ? "" : replay.error());
        expect(replay->steps.front().texts.at("app/main.ts") == start && replay->steps.back().texts.at("app/main.ts") == page)
            << "from the page omui new leaves to the page as it ships";
        expect(replay_text.contains("the sentence, in the person's words")) << "each step in its maker's words";
        pinned(here() / "design" / "main.replay.xdef.json", replay_text);

        // 8. Played in place as the Live Design key plays it, its course on the mark's O (omui.config.ts): at omui new's
        // page, which has no O, the card's own line; once a step's page has the O, the steps lie on it where it is
        // drawn and the card has no line.
        generate::Generation played(h.runtime(), {.replay = replay_text, .replay_file = "main.replay.xdef.json", .replay_course = "o",
                                                  .show = [&](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                                                      if (auto shown = app.show(texts); !shown) return std::unexpected(shown.error().message);
                                                      return {};
                                                  },
                                                  .restore = [&] { (void)app.discard_edits(); }});
        const auto steps_named = [&] { return h.tree().find({.role = Role::slider, .name = "Step"}); };
        (void)done("generate.replay", Value{}.set("at", 1));
        expect(!shown("o") && eq(steps_named().size(), std::size_t{1}) && steps_named().front().id.ends_with("/step")) << "no O yet: the card's line";
        (void)done("generate.replay", Value{}.set("at", 3));
        const auto on_o = steps_named();
        expect(fatal(shown("o") && eq(on_o.size(), std::size_t{1}))) << "the O drawn: one course";
        expect(on_o.front().id.ends_with("/course") && eq(on_o.front().value.get("value")->as_double(), 3.0)) << on_o.front().id;
        const NodeId course_node = h.runtime().find(on_o.front().id), o_node = h.runtime().find(path_of("o"));
        const auto* course = h.runtime().component<Timeline>(course_node);
        expect(fatal(course != nullptr));
        const Rect o_box = h.runtime().bounds(o_node), course_box = h.runtime().bounds(course_node);
        const Point at = course->point_at(3, course_box.size());
        expect(o_box.contains(Point{course_box.x + at.x, course_box.y + at.y})) << "the third step on the O, where it is drawn";
        (void)h.save_png(std::filesystem::temp_directory_path() / "omui-site-page-course.png");
        (void)done("generate.replay", Value{}.set("stop", true));
        expect(steps_named().empty() && h.tree().find({.role = Role::heading}).front().name.starts_with("A UI framework")) << "stopped: the page as it ships";

        std::ranges::sort(took);
        if (!took.empty())
            std::println("site page design: {} steps, {} edits, p95 {:.2f} ms", exported.get("steps") ? exported.get("steps")->as_int() : 0, took.size(),
                         took[std::min(took.size() - 1, static_cast<std::size_t>(std::ceil(took.size() * 0.95)) - 1)]);
    };
}};
