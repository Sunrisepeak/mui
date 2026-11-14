// Runtime generation (W5, D-144): drafts for a declared surface, through the
// stages one by one over the protocol — checked against what is registered,
// the limits and how open generation is; previewed with nothing left of it;
// committed as versions undo takes back.
import std;
import omui.agent;
import omui.generate;
import omui.viewdoc;
import omui.ui.base;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace agent = omui::agent;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
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
        return [this](Rect) -> std::expected<std::vector<std::uint8_t>, std::string> {
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

}  // namespace

UTest cases{"generation", [] {
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
        expect(std::filesystem::exists(dir / "index.json") && std::filesystem::exists(dir / "extras-2.view.omui.json"));
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
            std::ofstream(dir / "extras-1.view.omui.json")
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
