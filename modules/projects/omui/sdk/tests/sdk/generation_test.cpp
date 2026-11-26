// Runtime generation (W5, D-144): drafts for a declared surface, through the
// stages one by one over the protocol — checked against what is registered,
// the limits and how open generation is; previewed with nothing left of it;
// committed as versions undo takes back.
import std;
import mlibs.text.harfbuzz;
import omui.agent;
import omui.generate;
import omui.viewdoc;
import omui.ui.base;
import mlibs.agent;
import mlibs.image;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace agent = omui::agent;

namespace {

// This test program draws pixels and makes its runtimes itself (not through omui::run), so it is its own root
// (mlibs.compose): the morphs, pixel effects and picture reader a window's program has, installed before any case
// runs — once, for every file of tests/sdk/ (one program).
[[maybe_unused]] const bool pixel_parts = [] {
    (void)omui::install_morphs();
    (void)mrender::install_effects();
    (void)mlibs::image::install();
    return true;
}();

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made{mlibs::text::harfbuzz::shaper()};
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// A plan page with a place for what an agent adds (the slot `extras`).
struct Page {
    Runtime       rt;
    viewdoc::View view;
    int           pictures = 0;

    Page()
        : rt([] {
              RuntimeOptions options;
              options.fonts = &fonts();
              options.size = {320, 200};
              return options;
          }()),
          view(viewdoc::View::load(rt, R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [
              {"type": "text", "id": "plan", "props": {"text": "Basic plan"}}, {"slot": "extras"}]}}})j")
                   .value()) {
        (void)rt.verbs().add({"plan.upgrade", "Upgrades the plan", Effect::reversible, "", {}}, [](const Value&) -> VerbResult { return Value{}; });
        view.mount(rt, "main");
        (void)rt.frame({seconds(0), {}});
    }

    agent::CaptureFn capture() {
        return [this](Rect) -> std::expected<std::vector<std::uint8_t>, omui::ViewFailure> {
            ++pictures;
            return std::vector<std::uint8_t>{0x89, 'P', 'N', 'G'};
        };
    }

    // Whether a node named so is in the semantic tree, once what moves has settled.
    bool shows(std::string_view name) {
        (void)rt.frame({rt.now() + seconds(3), {}});
        const semantic::Snapshot now = semantic::Tree(rt).snapshot({});
        return std::ranges::any_of(now.nodes, [&](const spec::SemanticNode& node) { return node.name == name; });
    }
};

struct Answer {
    int   exit = -1;
    Value data;
    std::string error;
};

Answer talk(agent::Protocol& protocol, Value request) {
    Answer answer;
    protocol.request(request.json(), [&](std::string_view line) {
        const auto record = parse_json(line);
        if (!record) return;
        if (const Value* kind = record->get("kind"); kind && kind->as_string() == "data") answer.data = *record->get("data");
        if (const Value* kind = record->get("kind"); kind && kind->as_string() == "error") answer.error += std::string(line);
        if (const Value* kind = record->get("kind"); kind && kind->as_string() == "result") answer.exit = static_cast<int>(record->get("exitCode")->as_int());
    });
    return answer;
}

Answer invoke(agent::Protocol& protocol, std::string_view verb, Value args = Value(Value::Map{})) {
    return talk(protocol, Value{}.set("op", "invoke").set("verb", verb).set("args", std::move(args)));
}

Value card(std::string_view words) {
    return parse_json(std::format(R"j({{"omui": "0.1", "view": {{"root": {{"type": "text", "id": "offer", "props": {{"text": "{}"}}}}}}}})j", words)).value();
}

// The application's theme with an overlay of colours over it (what omui.theme's read_overlay and overlaid do for
// an application: here only `colors`, enough for the surface's steps).
std::expected<Theme, std::string> themed(std::string_view overlay) {
    Theme theme = Theme::light();
    if (overlay.empty()) return theme;
    const auto parsed = parse_json(overlay);
    if (!parsed || !parsed->map()) return std::unexpected(std::string("an overlay is an object of tokens"));
    if (const Value* colors = parsed->get("colors"); colors && colors->map())
        for (const auto& [name, value] : *colors->map()) {
            if (!theme.colors.contains(name)) return std::unexpected(std::format("/colors/{}: the theme has no colour `{}`", name, name));
            const auto color = spec::Color::parse(value.as_string());
            if (!color) return std::unexpected(std::format("/colors/{}: not a colour", name));
            theme.colors[name] = Color{color->r, color->g, color->b, color->a};
        }
    return theme;
}

}  // namespace

static UTest cases{"generation", [] {
    "the theme as a surface (G8): an overlay of tokens drafted, previewed still, committed as versions undo takes back, kept and shown again"_test = [] {
        const auto dir = std::filesystem::temp_directory_path() / std::format("omui-generation-theme-{}", std::random_device{}());
        std::filesystem::remove_all(dir);
        const Color violet = Color::hex(0x7c3aed), plain = Theme::light().color("accent");
        const auto same = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
        {
            Page page;
            generate::Generation generation(page.rt, {.openness = generate::Openness::data,
                                                      .surfaces = {{"look", "the colours a user's agent may change", generate::Surface::Kind::theme}},
                                                      .capture = page.capture(),
                                                      .theme = themed,
                                                      .directory = dir});
            agent::Protocol protocol(page.rt, {.capture = page.capture()});
            const Answer listed = invoke(protocol, "generate.surfaces");
            const Value& look = listed.data.get("surfaces")->list()->front();
            expect(look.get("kind")->as_string() == "theme" && look.get("shown")->as_bool()) << look.json();

            const Answer misspelt = invoke(protocol, "generate.draft", Value{}.set("surface", "look").set("document", parse_json(R"({"colors": {"acent": "#7c3aed"}})").value()));
            expect(eq(misspelt.exit, 1) && misspelt.error.contains("/colors/acent")) << misspelt.error;
            const Answer coded = invoke(protocol, "generate.draft", Value{}.set("surface", "look").set("code", "export default {}"));
            expect(eq(coded.exit, 1) && coded.error.contains("overlay of tokens")) << coded.error;

            const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", "look").set("document", parse_json(R"({"colors": {"accent": "#7c3aed"}})").value()));
            expect(eq(drafted.exit, 0) && drafted.data.get("stage")->as_string() == "prepared") << drafted.error;
            const std::string draft(drafted.data.get("draft")->as_string());
            expect(same(page.rt.theme().color("accent"), plain)) << "a draft is not shown";
            const Answer previewed = invoke(protocol, "generate.preview", Value{}.set("draft", draft));
            expect(eq(previewed.exit, 0) && previewed.data.get("before") && previewed.data.get("after")) << previewed.error;
            expect(same(page.rt.theme().color("accent"), plain)) << "the theme as it was after the preview";

            expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", draft)).exit, 0));
            expect(same(page.rt.theme().color("accent"), violet)) << "committed: the theme with the overlay";
            expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
            expect(same(page.rt.theme().color("accent"), plain)) << "undo: the theme with none";
            expect(eq(talk(protocol, Value{}.set("op", "redo")).exit, 0));
            expect(same(page.rt.theme().color("accent"), violet)) << "redo";
            const Answer versions = invoke(protocol, "generate.versions", Value{}.set("surface", "look"));
            expect(eq(versions.data.get("shown")->as_int(), std::int64_t{1}) && versions.data.json().contains("#7c3aed")) << versions.data.json();
        }
        expect(std::filesystem::exists(dir / "look-1.overlay.json"));
        {
            Page again;
            generate::Generation generation(again.rt, {.openness = generate::Openness::data,
                                                       .surfaces = {{"look", "", generate::Surface::Kind::theme}},
                                                       .theme = themed,
                                                       .directory = dir});
            (void)again.shows("Basic plan");  // the frames that show what was kept
            expect(same(again.rt.theme().color("accent"), violet)) << "a new start shows the version shown";
        }
        std::filesystem::remove_all(dir);
    };


    "kept in the host's store of text by key (the Web's page storage, where a page's files do not outlive it): a new start shows it again"_test = [] {
        std::map<std::string, std::string, std::less<>> page_storage;
        const auto read = [&](std::string_view name) -> std::optional<std::string> {
            const auto found = page_storage.find(name);
            return found == page_storage.end() ? std::nullopt : std::optional(found->second);
        };
        const auto write = [&](std::string_view name, std::string_view text) -> std::expected<void, std::string> {
            page_storage.insert_or_assign(std::string(name), std::string(text));
            return {};
        };
        const std::vector<generate::Surface> declared{{"extras", ""}, {"look", "", generate::Surface::Kind::theme}};
        {
            Page page;
            generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = declared, .theme = themed, .read_kept = read, .write_kept = write});
            agent::Protocol protocol(page.rt, {});
            for (const auto& [surface, document] : {std::pair{"extras", card("Pro")}, std::pair{"look", parse_json(R"({"colors": {"accent": "#7c3aed"}})").value()}}) {
                const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", surface).set("document", document));
                expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", drafted.data.get("draft")->as_string())).exit, 0)) << drafted.error;
            }
        }
        expect(page_storage.contains("index.json") && page_storage.contains("extras-1.view.xdef.json") && page_storage.contains("look-1.overlay.json"))
            << page_storage.size();
        Page again;
        generate::Generation generation(again.rt, {.openness = generate::Openness::data, .surfaces = declared, .theme = themed, .read_kept = read, .write_kept = write});
        expect(again.shows("Pro") && again.rt.theme().color("accent").r == Color::hex(0x7c3aed).r) << "shown again from the store";
    };

    "the customization as a layer of data (customization@1): exported, put over a new start, the same; one that does not pass changes nothing"_test = [] {
        const auto same = [](Color a, Color b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
        const std::vector<generate::Surface> declared{{"extras", "cards under the plan"}, {"look", "the colours", generate::Surface::Kind::theme}};
        Value layer;
        {
            Page page;
            generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = declared, .theme = themed});
            agent::Protocol protocol(page.rt, {});
            for (const auto& [surface, document] : {std::pair{"extras", card("Pro, 9 a month")}, std::pair{"look", parse_json(R"({"colors": {"accent": "#7c3aed"}})").value()}}) {
                const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", surface).set("document", document));
                expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", drafted.data.get("draft")->as_string())).exit, 0)) << drafted.error;
            }
            const Answer exported = invoke(protocol, "generate.export");
            expect(eq(exported.exit, 0) && exported.data.get("$format")->as_string() == "customization@1") << exported.data.json();
            layer = exported.data;
            expect(layer.json().contains("Pro, 9 a month") && layer.json().contains("#7c3aed") && layer.json().contains(R"("kind":"theme")")) << layer.json();
        }
        {
            Page again;
            generate::Generation generation(again.rt, {.openness = generate::Openness::data, .surfaces = declared, .theme = themed});
            agent::Protocol protocol(again.rt, {});
            // A layer with one surface that does not pass: nothing is put over the application.
            const Value broken = Value{}.set("$format", "customization@1").set("customization", Value{}.set("surfaces", Value{}
                .set("extras", *layer.get("customization")->get("surfaces")->get("extras"))
                .set("look", Value{}.set("kind", "theme").set("document", parse_json(R"({"colors": {"acent": "#000000"}})").value()))));
            const Answer refused = invoke(protocol, "generate.import", Value{}.set("document", broken));
            expect(eq(refused.exit, 1) && refused.error.contains("/customization/surfaces/look/document")) << refused.error;
            expect(!again.shows("Pro, 9 a month") && same(again.rt.theme().color("accent"), Theme::light().color("accent"))) << "nothing changed";
            expect(eq(invoke(protocol, "generate.import", Value{}.set("document", Value{}.set("customization", Value(Value::Map{})))).exit, 1)) << "no format: refused";

            const Answer imported = invoke(protocol, "generate.import", Value{}.set("document", layer));
            expect(eq(imported.exit, 0)) << imported.error;
            expect(again.shows("Pro, 9 a month") && same(again.rt.theme().color("accent"), Color::hex(0x7c3aed))) << "the same customization";
            expect(eq(invoke(protocol, "generate.export").data.json(), layer.json())) << "exported again: the same layer";
        }
    };

    "the design shown in place (G8): the replay it ships played in it, each step under its caption and who made it, as it ships at the end"_test = [] {
        Page page;
        mlibs::agent::Replay replay{.doc = "How the plan page was designed", .subject = "org.example.plan", .branch = "main"};
        replay.steps.push_back({.caption = "1/3 · the plan", .actor = "agent:designer", .seconds = 1, .texts = {{"app/main.ts", "Basic plan"}}, .node = "main/plan"});
        replay.steps.push_back({.caption = "2/3 · its price", .actor = "agent:designer", .seconds = 1, .texts = {{"app/main.ts", "Basic plan · $4"}}});
        replay.steps.push_back({.caption = "3/3 · in the person's words", .actor = "person:developer", .seconds = 1, .texts = {{"app/main.ts", "Your plan · $4"}}});
        // A stand-in for the player: a step's script is the plan's words; the application ships the last.
        std::vector<std::string> ran;
        const auto words = [&](std::string_view said) { page.rt.mount("main", text({.id = "plan", .text = std::string(said)})); };
        generate::Generation generation(page.rt, {.openness = generate::Openness::off,
                                                  .replay = mlibs::agent::write_replay(replay),
                                                  .replay_file = "plan.replay.xdef.json",
                                                  .show = [&](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                                                      ran.push_back(texts.at("app/main.ts"));
                                                      words(texts.at("app/main.ts"));
                                                      return {};
                                                  },
                                                  .restore = [&] {
                                                      ran.push_back("as it ships");
                                                      words("Your plan · $4");
                                                  }});
        agent::Protocol protocol(page.rt, {.capture = page.capture()});
        // What is on screen now, without time moving on.
        const auto on_screen = [&](std::string_view name) {
            (void)page.rt.frame({page.rt.now(), {}});
            const semantic::Snapshot now = semantic::Tree(page.rt).snapshot({});
            return std::ranges::any_of(now.nodes, [&](const spec::SemanticNode& node) { return node.name == name; });
        };
        const auto later = [&](double by) { (void)page.rt.frame({page.rt.now() + seconds(by), {}}); };

        const Answer listed = invoke(protocol, "generate.replay");
        expect(eq(listed.exit, 0) && eq(listed.data.get("steps")->list()->size(), std::size_t{3}) && eq(listed.data.get("at")->as_int(), std::int64_t{0}))
            << listed.error << listed.data.json();
        expect(eq(invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card("x"))).exit, 1)) << "generation is off; the replay is the application's own";

        const Answer playing = invoke(protocol, "generate.replay", Value{}.set("play", true));
        expect(eq(playing.exit, 0) && eq(playing.data.get("at")->as_int(), std::int64_t{1}) && playing.data.get("playing")->as_bool()) << playing.error;
        expect(on_screen("Basic plan") && on_screen("1/3 · the plan") && on_screen("designer · agent")) << "the first step, under its caption and who made it";
        const auto notes = page.rt.agent_notes();
        expect(notes.size() == 1 && notes.front().actor == "agent:designer" && notes.front().node == "main/plan" && notes.front().text == "the plan")
            << "its maker at what it built: a note there, which the presence shows";
        expect(on_screen("designer: the plan")) << "the presence over the page says it, the count left to the line";
        later(1.05);
        expect(on_screen("Basic plan · $4") && on_screen("2/3 · its price")) << "the next once its time is up";
        later(1.05);
        expect(on_screen("Your plan · $4") && on_screen("developer · person"));
        expect(page.rt.agent_notes().size() == 1 && page.rt.agent_notes().front().actor == "person:developer") << "one maker's note at a time";
        later(1.05);
        const Answer ended = invoke(protocol, "generate.replay");
        expect(eq(ended.data.get("at")->as_int(), std::int64_t{0}) && ended.data.get("ended")->as_bool() && !ended.data.get("playing")->as_bool()) << ended.data.json();
        expect(!on_screen("3/3 · in the person's words") && on_screen("Your plan · $4")) << "as it ships, the caption gone";
        expect(eq(ran, std::vector<std::string>{"Basic plan", "Basic plan · $4", "Your plan · $4", "as it ships"}));
        expect(page.rt.agent_notes().empty()) << "what the makers said, withdrawn";

        // A step gone to stays; pause holds it; stop is the application as it ships.
        const Answer second = invoke(protocol, "generate.replay", Value{}.set("at", 2));
        expect(eq(second.exit, 0) && !second.data.get("playing")->as_bool() && on_screen("Basic plan · $4")) << second.error;
        later(5);
        expect(on_screen("Basic plan · $4") && on_screen("2/3 · its price")) << "it stays there";
        const Answer out_of_range = invoke(protocol, "generate.replay", Value{}.set("at", 9));
        expect(eq(out_of_range.exit, 1) && out_of_range.error.contains("1 … 3")) << out_of_range.error;
        (void)invoke(protocol, "generate.replay", Value{}.set("play", true).set("speed", 4));
        later(0.3);
        expect(on_screen("Your plan · $4")) << "four times as fast: a quarter of a second each";
        (void)invoke(protocol, "generate.replay", Value{}.set("stop", true));
        expect(!on_screen("3/3 · in the person's words") && on_screen("Your plan · $4") && eq(ran.back(), std::string("as it ships")));

        // The person's own controls on its card: Pause, the timeline they scrub, Close.
        Invocation person;
        person.actor = {"person", "visitor", {}};
        const auto press = [&](std::string_view node, std::string_view action, Value args = Value{}) {
            person.args = std::move(args);
            const int code = semantic::Tree(page.rt).act(node, action, person).exit_code().value_or(-1);
            (void)page.rt.frame({page.rt.now(), {}});
            return code;
        };
        (void)invoke(protocol, "generate.replay", Value{}.set("play", true));
        expect(eq(press("generate.replay/dock/replay/controls/play", "activate"), 0) && !invoke(protocol, "generate.replay").data.get("playing")->as_bool() && on_screen("Play"))
            << "paused by its button, which now says Play";
        expect(eq(press("generate.replay/dock/replay/step", "set_value", Value{}.set("value", 3.0)), 0)) << "the timeline scrubbed to step 3";
        expect(eq(invoke(protocol, "generate.replay").data.get("at")->as_int(), std::int64_t{3}) && on_screen("Your plan · $4"));
        expect(eq(press("generate.replay/dock/replay/controls/close", "activate"), 0) && eq(invoke(protocol, "generate.replay").data.get("at")->as_int(), std::int64_t{0}) &&
               !on_screen("3/3 · in the person's words"))
            << "closed: the page as it ships";
    };

    "a replay that does not read: said, at its place in its file"_test = [] {
        Page page;
        generate::Generation unread(page.rt, {.replay = R"({"$format": "replay@1", "replay": {"texts": {}, "steps": []}})", .replay_file = "plan.replay.xdef.json"});
        agent::Protocol protocol(page.rt, {.capture = page.capture()});
        const Answer refused = invoke(protocol, "generate.replay");
        expect(eq(refused.exit, 1) && refused.error.contains("plan.replay.xdef.json") && refused.error.contains("/replay/steps")) << refused.error;
    };

    "a step that does not run stops the replay where it is, says why, and the application is as it ships"_test = [] {
        Page page;
        mlibs::agent::Replay replay{.subject = "org.example.plan"};
        replay.steps.push_back({.caption = "1/2 · the plan", .actor = "agent:designer", .seconds = 1, .texts = {{"app/main.ts", "Basic plan"}}});
        replay.steps.push_back({.caption = "2/2 · broken", .actor = "agent:designer", .seconds = 1, .texts = {{"app/main.ts", "broken"}}});
        bool restored = false;
        generate::Generation generation(page.rt, {.replay = mlibs::agent::write_replay(replay),
                                                  .show = [&](const std::map<std::string, std::string>& texts) -> std::expected<void, std::string> {
                                                      if (texts.at("app/main.ts") == "broken") return std::unexpected(std::string("app/main.ts:1:1: does not run"));
                                                      return {};
                                                  },
                                                  .restore = [&] { restored = true; }});
        agent::Protocol protocol(page.rt, {.capture = page.capture()});
        expect(eq(invoke(protocol, "generate.replay", Value{}.set("play", true)).exit, 0));
        (void)page.rt.frame({page.rt.now() + seconds(1.05), {}});
        const Answer state = invoke(protocol, "generate.replay");
        expect(eq(state.data.get("at")->as_int(), std::int64_t{0}) && restored && state.data.get("problem")->as_string().contains("step 2: app/main.ts:1:1"))
            << state.data.json();
        const Answer refused = invoke(protocol, "generate.replay", Value{}.set("at", 2));
        expect(eq(refused.exit, 1) && refused.error.contains("does not run")) << refused.error;
    };

    "data: a view document drafted and prepared, previewed with nothing left of it, committed as versions undo takes back"_test = [] {
        Page page;
        generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = {{"extras", "cards an agent adds under the plan"}},
                                              .capture = page.capture()});
        agent::Protocol protocol(page.rt, {.capture = page.capture()});
        const Answer surfaces = invoke(protocol, "generate.surfaces");
        expect(eq(surfaces.exit, 0) && surfaces.data.get("openness")->as_string() == "data") << surfaces.data.json();
        const Value& extras = surfaces.data.get("surfaces")->list()->front();
        expect(extras.get("name")->as_string() == "extras" && extras.get("shown")->as_bool() && eq(extras.get("version")->as_int(), std::int64_t{0}))
            << "the slot shows it, nothing in it yet";

        // Refused before anything is built: a misspelt component (at its place), a verb in data, an unknown surface, code.
        const Answer misspelt = invoke(protocol, "generate.draft",
                                       Value{}.set("surface", "extras").set("document", parse_json(R"({"omui": "0.1", "view": {"root": {"type": "txt"}}})").value()));
        expect(eq(misspelt.exit, 1) && misspelt.error.contains("txt") && misspelt.error.contains("/view/root")) << misspelt.error;
        const Answer acting = invoke(protocol, "generate.draft",
                                     Value{}.set("surface", "extras").set("document", parse_json(R"j({"omui": "0.1", "view": {"root": {"type": "box", "id": "go",
                                         "on": {"press": [{"verb": "plan.upgrade"}]}}}})j").value()));
        expect(eq(acting.exit, 1) && acting.error.contains("data only") && acting.error.contains("plan.upgrade")) << acting.error;
        expect(eq(invoke(protocol, "generate.draft", Value{}.set("surface", "header").set("document", card("Pro"))).exit, 1));
        const Answer coded = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("code", "export default {}"));
        expect(eq(coded.exit, 1) && coded.error.contains("data only")) << coded.error;

        // Drafted: prepared, nothing shown.
        const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card("Pro, 9 a month")));
        expect(eq(drafted.exit, 0) && drafted.data.get("stage")->as_string() == "prepared") << drafted.error;
        const std::string first(drafted.data.get("draft")->as_string());
        expect(!page.shows("Pro, 9 a month")) << "a draft is not shown";

        // Previewed: pictures before and after, the change it brings — and nothing left of it.
        const Answer previewed = invoke(protocol, "generate.preview", Value{}.set("draft", first));
        expect(eq(previewed.exit, 0) && previewed.data.get("before") && previewed.data.get("after")) << previewed.error;
        expect(previewed.data.get("changes")->json().contains("Pro, 9 a month")) << previewed.data.get("changes")->json();
        expect(!page.shows("Pro, 9 a month")) << "the surface as it was after the preview";
        expect(invoke(protocol, "generate.drafts").data.json().contains("previewed"));

        // Committed: version 1, shown; a second draft, version 2; undo goes back a version, then to none.
        const Answer committed = invoke(protocol, "generate.commit", Value{}.set("draft", first));
        expect(eq(committed.exit, 0) && eq(committed.data.get("version")->as_int(), std::int64_t{1})) << committed.error;
        // It comes in as a design step does (written in, settling from 92 %): smaller at first, then whole.
        {
            const seconds from = page.rt.now();
            std::vector<float> widths;
            for (const double at : {0.05, 0.2, 0.4, 3.0}) {
                (void)page.rt.frame({from + seconds(at), {}});
                widths.push_back(page.rt.drawn_bounds(page.rt.find("main/extras")).width);
            }
            expect(widths[1] > 0 && widths[1] < widths[3] * 0.99f && widths[2] >= widths[1] && widths[3] > 0)
                << std::format("{} {} {} {}", widths[0], widths[1], widths[2], widths[3]);
        }
        expect(page.shows("Pro, 9 a month"));
        const Answer again = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card("Team, 29 a month")));
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", again.data.get("draft")->as_string())).exit, 0));
        expect(page.shows("Team, 29 a month") && !page.shows("Pro, 9 a month"));
        expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
        expect(page.shows("Pro, 9 a month") && !page.shows("Team, 29 a month")) << "undo: the version before";
        expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
        expect(!page.shows("Pro, 9 a month")) << "undo again: nothing";
        expect(eq(talk(protocol, Value{}.set("op", "redo")).exit, 0));
        expect(page.shows("Pro, 9 a month")) << "redo";

        // Versions kept with who made them; restore is a step of its own.
        const Answer versions = invoke(protocol, "generate.versions", Value{}.set("surface", "extras"));
        expect(eq(versions.data.get("versions")->list()->size(), std::size_t{2}) && eq(versions.data.get("shown")->as_int(), std::int64_t{1}))
            << versions.data.json();
        expect(versions.data.get("versions")->list()->front().get("actor")->as_string().starts_with("agent")) << versions.data.json();
        expect(eq(invoke(protocol, "generate.restore", Value{}.set("surface", "extras").set("version", 2)).exit, 0));
        expect(page.shows("Team, 29 a month"));
        expect(eq(page.pictures, 2)) << "a picture before and after the preview";
    };

    "limited: code makes the document, sandboxed by the host; only the components and verbs listed; off takes nothing"_test = [] {
        Page page;
        int evaluated = 0;
        generate::Generation generation(page.rt, {.openness = generate::Openness::limited,
                                              .surfaces = {{"extras", ""}},
                                              .components = {"text", "column"},
                                              .verbs = {},
                                              .evaluate = [&](std::string_view language, std::string_view code) -> std::expected<std::string, std::string> {
                                                  ++evaluated;
                                                  if (language != "ts") return std::unexpected(std::string("only ts"));
                                                  return std::string(code);  // the stand-in evaluator: the code is its document
                                              }});
        agent::Protocol protocol(page.rt, {});
        const Answer surfaces = invoke(protocol, "generate.surfaces");
        expect(surfaces.data.get("code")->as_bool() && eq(surfaces.data.get("components")->list()->size(), std::size_t{2})) << surfaces.data.json();
        const Answer boxed = invoke(protocol, "generate.draft",
                                    Value{}.set("surface", "extras").set("document", parse_json(R"({"omui": "0.1", "view": {"root": {"type": "box"}}})").value()));
        expect(eq(boxed.exit, 1) && boxed.error.contains("`box` is not among the components")) << boxed.error;
        const Answer made = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("language", "ts").set("code", card("From code").json()));
        expect(eq(made.exit, 0) && eq(evaluated, 1)) << made.error;
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", made.data.get("draft")->as_string())).exit, 0));
        expect(page.shows("From code"));

        Page closed;
        generate::Generation off(closed.rt, {.openness = generate::Openness::off, .surfaces = {{"extras", ""}}});
        agent::Protocol asking(closed.rt, {});
        const Answer refused = invoke(asking, "generate.draft", Value{}.set("surface", "extras").set("document", card("x")));
        expect(eq(refused.exit, 1) && refused.error.contains("off")) << refused.error;
    };

    "a version of several nodes undone: each one leaves and is gone (none lingers in the tree)"_test = [] {
        Page page;
        generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = {{"extras", ""}}});
        agent::Protocol protocol(page.rt, {});
        const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", parse_json(R"({"omui": "0.1", "view": {"root": {"type": "column", "children": [
            {"type": "text", "id": "pro", "props": {"text": "Pro"}}, {"type": "text", "id": "team", "props": {"text": "Team"}}]}}})").value()));
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", drafted.data.get("draft")->as_string())).exit, 0));
        for (int frame = 1; frame <= 120; ++frame) (void)page.rt.frame({page.rt.now() + seconds(1.0 / 60), {}});
        expect(static_cast<bool>(page.rt.find("main/extras/team")));
        expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
        for (int frame = 1; frame <= 180; ++frame) (void)page.rt.frame({page.rt.now() + seconds(1.0 / 60), {}});
        expect(!page.rt.find("main/extras/team") && !page.rt.find("main/extras")) << "gone once played back";
    };

    "kept on disk: a new start shows the version shown, checked again — one that no longer passes is not shown, and says why"_test = [] {
        const auto dir = std::filesystem::temp_directory_path() / std::format("omui-generation-{}", std::random_device{}());
        std::filesystem::remove_all(dir);
        {
            Page page;
            generate::Generation generation(page.rt, {.openness = generate::Openness::all, .surfaces = {{"extras", ""}}, .directory = dir});
            agent::Protocol protocol(page.rt, {});
            for (const std::string_view words : {"Pro", "Team"}) {
                const Answer drafted = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card(words)));
                expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", drafted.data.get("draft")->as_string())).exit, 0));
            }
            expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0)) << "Pro shown when it ends";
        }
        expect(std::filesystem::exists(dir / "index.json") && std::filesystem::exists(dir / "extras-2.view.xdef.json"));
        {
            Page again;
            generate::Generation generation(again.rt, {.openness = generate::Openness::all, .surfaces = {{"extras", ""}}, .directory = dir});
            agent::Protocol protocol(again.rt, {});
            expect(again.shows("Pro") && !again.shows("Team")) << "the version shown, shown again";
            const Answer versions = invoke(protocol, "generate.versions", Value{}.set("surface", "extras"));
            expect(eq(versions.data.get("versions")->list()->size(), std::size_t{2}) && eq(versions.data.get("shown")->as_int(), std::int64_t{1}));
        }
        {
            // Started again more closed: data takes no verbs — and a kept version that invokes one is not shown.
            std::ofstream(dir / "extras-1.view.xdef.json")
                << R"j({"omui": "0.1", "view": {"root": {"type": "box", "id": "go", "label": "Pro", "on": {"press": [{"verb": "plan.upgrade"}]}}}})j";
            Page closed;
            generate::Generation generation(closed.rt, {.openness = generate::Openness::data, .surfaces = {{"extras", ""}}, .directory = dir});
            agent::Protocol protocol(closed.rt, {});
            expect(!closed.shows("Pro"));
            const Answer surfaces = invoke(protocol, "generate.surfaces");
            const Value& extras = surfaces.data.get("surfaces")->list()->front();
            expect(eq(extras.get("version")->as_int(), std::int64_t{0}) && extras.get("problem")->as_string().contains("data only")) << surfaces.data.json();
        }
        std::filesystem::remove_all(dir);
    };

    "A2UI: a stream's messages kept between calls, each rendering surface drafted and committed as it comes; a button's action handed back; what is not read is named"_test = [] {
        register_base_components();
        Page page;
        generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = {{"extras", ""}}});
        agent::Protocol protocol(page.rt, {});
        // The first chunk (JSON lines, a message a line): the components and the data, not yet rendering — nothing drafted.
        const std::string first = parse_json(
            R"({"surfaceUpdate": {"surfaceId": "booking", "components": [
                {"id": "root", "component": {"Card": {"child": "body"}}},
                {"id": "body", "component": {"Column": {"children": {"explicitList": ["title", "guests", "notes", "book"]}}}},
                {"id": "title", "component": {"Text": {"text": {"literalString": "Table for two"}, "usageHint": "h2"}}},
                {"id": "guests", "component": {"Slider": {"value": {"path": "/reservation/guests"}, "minValue": 1, "maxValue": 8}}},
                {"id": "notes", "component": {"TextField": {"label": {"literalString": "Notes"}, "text": {"path": "/reservation/notes"}}}},
                {"id": "book", "component": {"Button": {"child": "book_label", "primary": true,
                    "action": {"name": "book", "context": [{"key": "guests", "value": {"path": "/reservation/guests"}}]}}}},
                {"id": "book_label", "component": {"Text": {"text": {"literalString": "Book"}}}}]}})").value().json() +
            "\n" + parse_json(R"({"dataModelUpdate": {"surfaceId": "booking", "path": "/reservation", "contents": [{"key": "guests", "valueNumber": 2}, {"key": "notes", "valueString": ""}]}})").value().json();
        const Answer kept = invoke(protocol, "generate.a2ui", Value{}.set("messages", first).set("surface", "extras").set("commit", true));
        expect(eq(kept.exit, 0) && kept.data.get("drafts")->list()->empty()) << kept.error << kept.data.json();
        // Rendering begins: drafted, committed, shown — the slider at the data's 2.
        const Answer shown = invoke(protocol, "generate.a2ui",
                                    Value{}.set("messages", parse_json(R"({"beginRendering": {"surfaceId": "booking", "root": "root"}})").value())
                                        .set("surface", "extras").set("commit", true));
        expect(eq(shown.exit, 0) && eq(shown.data.get("drafts")->list()->size(), std::size_t{1})) << shown.error;
        expect(page.shows("Table for two") && page.shows("Book"));
        const semantic::Snapshot now = semantic::Tree(page.rt).snapshot({});
        const auto slider = std::ranges::find_if(now.nodes, [](const spec::SemanticNode& node) { return node.id.ends_with("/guests"); });
        expect(slider != now.nodes.end() && slider->value.json().contains("2")) << (slider != now.nodes.end() ? slider->value.json() : std::string("no slider"));
        // The person books: the action, with its context read from the data, handed back.
        Invocation person;
        person.actor = {"person", "guest", {}};
        const auto book = std::ranges::find_if(now.nodes, [](const spec::SemanticNode& node) { return node.id.ends_with("/book"); });
        expect(book != now.nodes.end());
        if (book != now.nodes.end()) (void)semantic::Tree(page.rt).act(book->id, "activate", person);
        const Answer acted = invoke(protocol, "generate.actions");
        expect(eq(acted.data.get("actions")->list()->size(), std::size_t{1})) << acted.data.json();
        if (!acted.data.get("actions")->list()->empty()) {
            const Value& action = acted.data.get("actions")->list()->front();
            expect(action.get("name")->as_string() == "book" && action.get("sourceComponentId")->as_string() == "book" &&
                   action.get("surfaceId")->as_string() == "booking")
                << action.json();
            expect(action.get("context") && eq(action.get("context")->get("guests")->as_double(), 2.0)) << action.json();
        }
        expect(invoke(protocol, "generate.actions", Value{}.set("since", acted.data.get("last")->as_int())).data.get("actions")->list()->empty());
        // A later update to the same surface: a new version, the rest kept.
        const Answer changed = invoke(protocol, "generate.a2ui",
                                      Value{}.set("messages", parse_json(R"({"surfaceUpdate": {"surfaceId": "booking", "components": [
                                          {"id": "title", "component": {"Text": {"text": {"literalString": "Table for four"}}}}]}})").value())
                                          .set("surface", "extras").set("commit", true));
        expect(eq(changed.exit, 0) && eq(changed.data.get("drafts")->list()->front().get("version")->as_int(), std::int64_t{2})) << changed.error;
        expect(page.shows("Table for four") && page.shows("Book"));
        // Not read yet, or not sent: named, nothing drafted.
        const Answer unread = invoke(protocol, "generate.a2ui",
                                     Value{}.set("messages", parse_json(R"([{"surfaceUpdate": {"surfaceId": "more", "components": [
                                         {"id": "root", "component": {"Column": {"children": {"explicitList": ["when", "missing"]}}}},
                                         {"id": "when", "component": {"DateTimeInput": {}}}]}}, {"beginRendering": {"surfaceId": "more", "root": "root"}}])").value())
                                         .set("surface", "extras"));
        expect(eq(unread.exit, 1) && unread.error.contains("DateTimeInput is not read yet") && unread.error.contains("`missing` is named but was not sent"))
            << unread.error;
        // deleteSurface: the surface goes, a step undo takes back.
        expect(eq(invoke(protocol, "generate.a2ui", Value{}.set("messages", parse_json(R"({"deleteSurface": {"surfaceId": "booking"}})").value())
                                                    .set("surface", "extras").set("commit", true)).exit, 0));
        expect(!page.shows("Table for four"));
        expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
        expect(page.shows("Table for four"));
    };

    "two drafts over the same version: the second to commit is a conflict unless it says `over`; a version kept as a piece is drafted again elsewhere"_test = [] {
        Page page;
        generate::Generation generation(page.rt, {.openness = generate::Openness::data, .surfaces = {{"extras", ""}}});
        agent::Protocol protocol(page.rt, {});
        const Answer one = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card("Pro")));
        const Answer two = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("document", card("Team")));
        expect(eq(one.data.get("over")->as_int(), std::int64_t{0}) && one.data.get("set")->get("writes")->get("surface")->as_string() == "extras")
            << one.data.json();
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", one.data.get("draft")->as_string())).exit, 0));
        const Answer late = invoke(protocol, "generate.commit", Value{}.set("draft", two.data.get("draft")->as_string()));
        expect(eq(late.exit, 1) && late.error.contains("conflict") && late.error.contains("version 1")) << late.error;
        expect(page.shows("Pro") && !page.shows("Team")) << "nothing replaced";
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", two.data.get("draft")->as_string()).set("over", true)).exit, 0));
        expect(page.shows("Team"));

        const Answer kept = invoke(protocol, "generate.keep", Value{}.set("surface", "extras").set("version", 1).set("name", "pro_card").set("doc", "the Pro plan"));
        expect(eq(kept.exit, 0)) << kept.error;
        expect(invoke(protocol, "generate.pieces").data.json().contains("pro_card"));
        const Answer again = invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("piece", "pro_card"));
        expect(eq(invoke(protocol, "generate.commit", Value{}.set("draft", again.data.get("draft")->as_string())).exit, 0)) << again.error;
        expect(page.shows("Pro") && !page.shows("Team")) << "the piece, reused";
        expect(eq(invoke(protocol, "generate.forget", Value{}.set("name", "pro_card")).exit, 0));
        expect(eq(invoke(protocol, "generate.draft", Value{}.set("surface", "extras").set("piece", "pro_card")).exit, 1));
    };

    "a draft in the background (W8): its handle at once; frames, input and requests go on; a cancel is taken at once and its draft never shows; the next one commits"_test = [] {
        Page page;
        agent::TaskBroker tasks(page.rt);
        // The code's evaluation waits until the test lets it go: the work is in flight for as long as the test says.
        std::atomic<int> released{0};
        generate::Generation generation(page.rt, {.openness = generate::Openness::all,
                                              .surfaces = {{"extras", ""}},
                                              .evaluate = [&](std::string_view, std::string_view code) -> std::expected<std::string, std::string> {
                                                  const int mine = code.contains("first") ? 1 : 2;
                                                  for (int wait = 0; wait < 4000 && released.load() < mine; ++wait) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                                  return card(code.contains("first") ? "First idea" : "Second idea").json();
                                              },
                                              .tasks = &tasks});
        agent::Protocol protocol(page.rt, {});
        const auto asked = std::chrono::steady_clock::now();
        const Answer first = invoke(protocol, "generate.draft",
                                    Value{}.set("surface", "extras").set("code", "first").set("language", "ts").set("background", true).set("commit", true));
        expect(eq(first.exit, 0) && first.data.get("task")) << first.error;
        expect(std::chrono::steady_clock::now() - asked < std::chrono::milliseconds(500)) << "answered without waiting for the work";
        const std::string first_task(first.data.get("task")->as_string());
        // Meanwhile: frames, a person's press, another request — all at once.
        for (int frame = 0; frame < 10; ++frame) (void)page.rt.frame({page.rt.now() + seconds(1.0 / 60), {}});
        expect(eq(invoke(protocol, "generate.surfaces").exit, 0));
        const Answer running = invoke(protocol, "task.status", Value{}.set("task", first_task));
        expect(running.data.get("state")->as_string() == "running") << running.data.json();
        // The person changes their mind: the first cancelled at once (its work still stuck), the second asked for.
        const Answer cancelled = invoke(protocol, "task.cancel", Value{}.set("task", first_task));
        expect(eq(cancelled.exit, 0) && cancelled.data.get("state")->as_string() == "cancelled") << cancelled.error << cancelled.data.json();
        const Answer second = invoke(protocol, "generate.draft",
                                     Value{}.set("surface", "extras").set("code", "second").set("language", "ts").set("background", true).set("commit", true));
        released = 2;  // both works go on now
        for (int frame = 0; frame < 600 && invoke(protocol, "task.status", Value{}.set("task", second.data.get("task")->as_string())).data.get("state")->as_string() == "running"; ++frame) {
            (void)page.rt.frame({page.rt.now() + seconds(1.0 / 60), {}});
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // The first's work ended too; let its end arrive: still nothing of it.
        for (int frame = 0; frame < 30; ++frame) {
            (void)page.rt.frame({page.rt.now() + seconds(1.0 / 60), {}});
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const Answer done = invoke(protocol, "task.status", Value{}.set("task", second.data.get("task")->as_string()));
        expect(done.data.get("state")->as_string() == "done" && eq(done.data.get("result")->get("version")->as_int(), std::int64_t{1})) << done.data.json();
        expect(page.shows("Second idea") && !page.shows("First idea")) << "the cancelled draft never shows";
        expect(invoke(protocol, "task.status", Value{}.set("task", first_task)).data.get("state")->as_string() == "cancelled");
        expect(eq(invoke(protocol, "task.list").data.get("tasks")->list()->size(), std::size_t{2}));
        // Committed in the name of who asked, as a step of the history.
        expect(eq(talk(protocol, Value{}.set("op", "undo")).exit, 0));
        expect(!page.shows("Second idea")) << "undo takes a background commit back";
    };

    "limits and places: a document over the limits is refused; a surface nothing shows takes no commit"_test = [] {
        Page page;
        generate::Generation generation(page.rt, {.openness = generate::Openness::all, .surfaces = {{"extras", ""}, {"footer", "not on this page"}}, .max_nodes = 2});
        agent::Protocol protocol(page.rt, {});
        const Answer many = invoke(protocol, "generate.draft",
                                   Value{}.set("surface", "extras").set("document", parse_json(R"({"omui": "0.1", "view": {"root": {"type": "column", "children": [
                                       {"type": "text", "props": {"text": "a"}}, {"type": "text", "props": {"text": "b"}}]}}})").value()));
        expect(eq(many.exit, 1) && many.error.contains("3 nodes")) << many.error;
        const Answer elsewhere = invoke(protocol, "generate.draft", Value{}.set("surface", "footer").set("document", card("Help")));
        expect(eq(elsewhere.exit, 0)) << elsewhere.error;
        const Answer unseen = invoke(protocol, "generate.commit", Value{}.set("draft", elsewhere.data.get("draft")->as_string()));
        expect(eq(unseen.exit, 1) && unseen.error.contains("not on screen")) << unseen.error;
        expect(!page.shows("Help"));
        expect(eq(invoke(protocol, "generate.discard", Value{}.set("draft", elsewhere.data.get("draft")->as_string())).exit, 0));
        expect(invoke(protocol, "generate.drafts").data.get("drafts")->list()->empty());
    };
}};
