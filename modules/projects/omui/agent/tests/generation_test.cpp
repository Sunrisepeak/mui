// Runtime generation (W5, D-144): drafts for a declared surface, through the
// stages one by one over the protocol — checked against what is registered,
// the limits and how open generation is; previewed with nothing left of it;
// committed as versions undo takes back.
import std;
import omui.agent;
import omui.viewdoc;
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
        agent::Generation generation(page.rt, {.openness = agent::Openness::data, .surfaces = {{"extras", "cards an agent adds under the plan"}},
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
        agent::Generation generation(page.rt, {.openness = agent::Openness::limited,
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
        agent::Generation off(closed.rt, {.openness = agent::Openness::off, .surfaces = {{"extras", ""}}});
        agent::Protocol asking(closed.rt, {});
        const Answer refused = invoke(asking, "generate.draft", Value{}.set("surface", "extras").set("document", card("x")));
        expect(eq(refused.exit, 1) && refused.error.contains("off")) << refused.error;
    };

    "a version of several nodes undone: each one leaves and is gone (none lingers in the tree)"_test = [] {
        Page page;
        agent::Generation generation(page.rt, {.openness = agent::Openness::data, .surfaces = {{"extras", ""}}});
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

    "limits and places: a document over the limits is refused; a surface nothing shows takes no commit"_test = [] {
        Page page;
        agent::Generation generation(page.rt, {.openness = agent::Openness::all, .surfaces = {{"extras", ""}, {"footer", "not on this page"}}, .max_nodes = 2});
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
