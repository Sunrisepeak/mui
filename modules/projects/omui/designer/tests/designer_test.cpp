// The designer (Live Design, D-149, D-151) over a player in development mode:
// design edits run at once and written back, where a node was built and back,
// structured edits, the history and its merges, previews and variants in
// stand-ins, the timeline, a person's demonstration kept as a test.
import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.player;
import omui.designer;
import omui.semantic;
import omui.ui.base;
import omui.host.headless;
import mlibs.image;
import mlibs.testing;
import mcpplibs.mtest;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace mcpplibs::mtest;
using namespace omui;

namespace {

void write(const std::filesystem::path& p, std::string_view text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
    // A modification time no file system can confuse with the last one.
    static int bump = 0;
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++bump));
}

// A language for the test: a "script" is a view document, mounted at "side"
// through the C ABI, as a binding's mount would.
class Stub final : public player::Engine {
public:
    explicit Stub(abi::OmuiRuntime rt) : rt_(rt) {}
    std::future<player::Status> run(std::string code, std::string) override { return mount(code); }
    std::future<player::Status> reload(std::string code, std::string) override { return mount(code); }
    player::Status reload_now(std::string code, std::string) override { return mount(code).get(); }
    [[nodiscard]] bool idle() const override { return true; }

private:
    std::future<player::Status> mount(const std::string& code) {
        std::promise<player::Status> p;
        if (abi::omuiMount(rt_, {"side", 4}, {code.data(), code.size()}) != abi::OMUI_SUCCESS) p.set_value(std::unexpected(std::string("refused")));
        else p.set_value({});
        return p.get_future();
    }
    abi::OmuiRuntime rt_;
};

// A language whose script imports one module by path (logic/part.stub): it
// says so, and each run mounts that module's text (its edit, or the file's).
struct ModuleLog {
    std::vector<std::string> changes;  // module_changed: "<file>=<text or (file)>"
    int                      runs = 0;
};

class ModuleStub final : public player::Engine {
public:
    ModuleStub(abi::OmuiRuntime rt, std::filesystem::path part, ModuleLog& log) : rt_(rt), part_(std::move(part)), log_(log) {}
    std::future<player::Status> run(std::string, std::string) override { return ready(mount()); }
    std::future<player::Status> reload(std::string, std::string) override { return ready(mount()); }
    player::Status reload_now(std::string, std::string) override { return mount(); }
    [[nodiscard]] bool idle() const override { return true; }
    void module_changed(const std::filesystem::path& file, std::optional<std::string> text) override {
        log_.changes.push_back(std::format("{}={}", file.filename().generic_string(), text ? *text : std::string("(file)")));
        edit_ = std::move(text);
    }
    [[nodiscard]] std::vector<std::filesystem::path> modules() const override { return {part_}; }

private:
    static std::future<player::Status> ready(player::Status s) {
        std::promise<player::Status> p;
        p.set_value(std::move(s));
        return p.get_future();
    }
    player::Status mount() {
        ++log_.runs;
        std::ifstream in(part_, std::ios::binary);
        const std::string code = edit_ ? *edit_ : std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        if (abi::omuiMount(rt_, {"side", 4}, {code.data(), code.size()}) != abi::OMUI_SUCCESS) return std::unexpected(std::string("refused"));
        return {};
    }
    abi::OmuiRuntime             rt_;
    std::filesystem::path        part_;
    ModuleLog&                   log_;
    std::optional<std::string>   edit_;
};

player::Language stub() {
    return {"stub", {".stub"}, [](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<Stub>(rt); }, {}};
}

// A twin as the entry makes one, over the test's language: the application
// again, headless, its time stepped by the caller.
class StubTwin final : public player::Twin {
public:
    StubTwin(const player::Manifest& manifest, bool design)
        : host_(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {320, 240}}), shown_(host_.runtime(), manifest, {stub()}, {.design = design}) {
        (void)shown_.start();
        host_.settle();
    }
    player::Player& player() override { return shown_; }
    Runtime& runtime() override { return host_.runtime(); }
    seconds time() override { return host_.runtime().now(); }
    void step(std::optional<seconds> at) override { (void)host_.step({}, at); }
    void settle() override { host_.settle(); }
    std::expected<std::vector<std::uint8_t>, std::string> png() override { return host_.png(); }

private:
    Headless       host_;
    player::Player shown_;
};

player::TwinFn stub_twins(const player::Manifest& manifest, int* made = nullptr) {
    return [manifest, made](bool design) -> std::expected<std::unique_ptr<player::Twin>, std::string> {
        if (made) ++*made;
        return std::make_unique<StubTwin>(manifest, design);
    };
}

constexpr std::string_view counter = R"j({"omui": "0.1", "view": {"signals": {"count": {"type": "int"}},
  "root": {"type": "column", "children": [
    {"type": "tally_tag", "id": "badge", "props": {"label": "clicks"}},
    {"type": "text", "id": "n", "props": {"text": {"expr": "format('{} clicks', count)"}}},
    {"type": "button", "id": "inc", "props": {"text": "+1"}, "on": {"activated": [{"set": "count", "expr": "count + 1"}]}}]}}})j";

constexpr std::string_view badge_doc = R"j({"omui": "0.1", "component": {"name": "tally_tag",
  "interface": {"props": {"label": {"type": "string"}}},
  "root": {"type": "text", "id": "t", "props": {"text": {"expr": "upper(label)"}}}}})j";

std::string file_text(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string name_of(Headless& h, std::string_view id) {
    auto n = h.tree().node(id);
    return n ? n->name : std::string("<none>");
}

}  // namespace

UTest cases{"designer", [] {
    register_base_components();

    "development mode: design.edit runs new text at once — its semantic changes, or its problems with nothing changed; discard and commit"_test = [] {
        const auto dir = scratch_dir("player-design");
        const std::string first = R"j({"omui": "0.1", "view": {"root": {"type": "text", "id": "hello", "props": {"text": "first"}}}})j";
        write(dir / "logic" / "side.stub", first);
        player::Manifest m;
        m.id = "org.openmui.player-design";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            return h.runtime().verbs().invoke(verb, call);
        };
        const auto data = [](const Envelope& answer) { return answer.data_value() ? *answer.data_value() : Value{}; };

        const auto sources = data(invoke("design.sources"));
        const Value* script = sources.get("scripts") && sources.get("scripts")->list() && !sources.get("scripts")->list()->empty()
                                  ? &sources.get("scripts")->list()->front()
                                  : nullptr;
        expect(script && script->get("path")->as_string() == "logic/side.stub" && script->get("text")->as_string() == first &&
               !script->get("edited")->as_bool())
            << sources.json();

        // An edit: run at once, its changes answered, the file untouched.
        std::string second = first;
        second.replace(second.find("first"), 5, "second");
        const auto edited = invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", second));
        expect(eq(edited.exit_code().value_or(-1), 0)) << edited.ndjson();
        const Value answer = data(edited);
        const Value* changes = answer.get("changes");
        expect(changes && changes->get("changed") && changes->get("changed")->list() && changes->get("changed")->list()->size() == 1) << edited.ndjson();
        expect(changes && changes->json().contains("\"before\":\"first\"") && changes->json().contains("\"after\":\"second\"")) << edited.ndjson();
        expect(answer.get("took_ms") && answer.get("took_ms")->as_double() >= 0);
        expect(eq(name_of(h, "side"), std::string("second")));
        // The code it wrote, beside what it changed, for a moment (a frame on: settling waits out the presence, longer than the card).
        (void)h.step();
        const auto card = h.tree().snapshot({.root = "design-code/card"}).to_value().json();
        expect(card.contains("logic/side.stub:1") && card.contains("+ ") && card.contains("second")) << card;
        (void)h.step({}, h.runtime().now() + seconds(3.5));
        h.settle();
        expect(!h.tree().node("design-code/card").has_value()) << "gone after a moment";
        expect(eq(file_text(dir / "logic" / "side.stub"), first)) << "an edit stays in the session until committed";
        // The agent's edit is said beside what it changed (its presence shows it).
        const auto notes = h.runtime().agent_notes();
        expect(!notes.empty() && notes.back().actor == "agent:designer" && notes.back().text.starts_with("logic/side.stub: 1 change — changed ") &&
               !notes.back().node.empty())
            << (notes.empty() ? std::string("no note") : notes.back().text);

        // A text that does not run: refused with its problem, nothing changed.
        const auto refused = invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", "{\"omui\": \"0.1\", \"view\": {\"root\": {\"type\": \"colum\"}}}"));
        expect(refused.exit_code().value_or(0) != 0) << refused.ndjson();
        h.settle();
        expect(eq(name_of(h, "side"), std::string("second"))) << "what ran before runs";
        expect(invoke("design.edit", Value{}.set("path", "logic/nothing.stub").set("text", first)).exit_code().value_or(0) != 0);

        // What the person expects, checked as facts against the tree as it is.
        expect(eq(invoke("design.expect", Value{}.set("say", "the side panel says second").set("query", Value{}.set("id", "side").set("name", "second")))
                      .exit_code()
                      .value_or(-1),
                  0));
        (void)invoke("design.expect", Value{}.set("say", "a help link is there").set("query", Value{}.set("id", "side/help")));
        const auto checked = invoke("design.check");
        const Value facts = data(checked);
        expect(facts.get("held") && facts.get("held")->as_int() == 1 && facts.get("of")->as_int() == 2) << checked.ndjson();
        expect(checked.ndjson().contains("\"why\":\"no node side/help\"")) << checked.ndjson();
        // Discard: the file's text runs again; the dropped edit answered.
        const auto discarded = invoke("design.discard");
        expect(eq(discarded.exit_code().value_or(-1), 0) && discarded.ndjson().contains("second")) << discarded.ndjson();
        expect(eq(name_of(h, "side"), std::string("first")));

        // Commit: the edit written to the file, the text before answered; the watcher does not run it again.
        (void)invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", second));
        const auto committed = invoke("design.commit");
        expect(eq(committed.exit_code().value_or(-1), 0) && committed.ndjson().contains("\"before\"")) << committed.ndjson();
        expect(eq(file_text(dir / "logic" / "side.stub"), second));
        expect(p.poll().empty()) << "committed text is not run twice";
        expect(eq(name_of(h, "side"), std::string("second")));
    };

    "design.locate: a node to the line of the script that built it, and a line to the nodes built there"_test = [] {
        const auto dir = scratch_dir("player-locate");
        // Where a script engine says its elements were built, in development mode (built_at: file:line:column).
        const std::string file = generic_utf8(dir / "logic" / "side.stub");
        const std::string doc = std::format(R"j({{"omui": "0.1", "view": {{"root": {{"type": "column", "built_at": "{0}:1:30", "children": [
  {{"type": "text", "id": "hello", "built_at": "{0}:2:3", "props": {{"text": "hi"}}}},
  {{"type": "text", "id": "bye", "built_at": "{0}:3:3", "props": {{"text": "bye"}}}},
  {{"type": "text", "id": "later", "built_at": "{0}:3:60", "props": {{"text": "later"}}}}]}}}}}})j",
                                            file);
        write(dir / "logic" / "side.stub", doc);
        player::Manifest m;
        m.id = "org.openmui.player-locate";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](Value args) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke("design.locate", call);
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{}};
        };
        const auto [found, place] = invoke(Value{}.set("node", "side/hello"));
        expect(eq(found, 0)) << place.json();
        expect(place.get("path") && place.get("path")->as_string() == "logic/side.stub") << place.json();
        expect(place.get("line") && place.get("line")->as_int() == 2 && place.get("column")->as_int() == 3) << place.json();
        expect(place.get("text") && place.get("text")->as_string().contains("\"id\": \"hello\"")) << place.json() << " — the line's text";
        const auto [listed, nodes] = invoke(Value{}.set("path", "logic/side.stub").set("line", 3));
        expect(eq(listed, 0)) << nodes.json();
        expect(nodes.get("nodes") && nodes.get("nodes")->json().contains("side/bye") && nodes.get("nodes")->json().contains("side/later") &&
               !nodes.get("nodes")->json().contains("side/hello"))
            << nodes.json();
        expect(invoke(Value{}.set("node", "side/none")).first != 0) << "no such node";
        expect(invoke(Value{}.set("path", "logic/other.stub").set("line", 1)).first != 0) << "no such script";
        expect(invoke(Value{}.set("path", "logic/side.stub")).first != 0) << "a path needs a line";
    };

    "the design's history: commit, branch, switch, merge line by line, diff, tag, revert — under .omui/design/, nothing lost"_test = [] {
        const auto dir = scratch_dir("player-history");
        std::error_code error;
        std::filesystem::remove_all(dir / ".omui", error);
        const std::string base = R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [
  {"type": "text", "id": "hello", "props": {"text": "first"}},
  {"type": "text", "id": "bye", "props": {"text": "bye"}}]}}}
)j";
        write(dir / "logic" / "side.stub", base);
        player::Manifest m;
        m.id = "org.openmui.player-history";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            h.settle();
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{answer.ndjson()}};
        };
        const auto edit = [&](std::string_view from, std::string_view to) {
            const std::string now = file_text(dir / "logic" / "side.stub");
            const auto sources = invoke("design.sources").second;
            std::string text(sources.get("scripts")->list()->front().get("text")->as_string());
            text.replace(text.find(from), from.size(), to);
            return invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", text)).first;
        };
        const auto commit = [&](std::string_view message) { return invoke("design.history.commit", Value{}.set("message", message)); };

        expect(invoke("design.history").second.get("uncommitted")->as_bool()) << "nothing committed yet";
        const auto [began, begin_answer] = invoke("design.begin", Value{}.set("intent", "greetings"));
        expect(eq(began, 0) && begin_answer.get("commit")->type() == Value::Type::text) << begin_answer.json() << " — the state it begins from";
        expect(commit("again").first != 0) << "nothing changed: no commit";
        expect(eq(invoke("design.history.branch", Value{}.set("name", "quiet")).first, 0));
        // Each step is a commit of its own, saying the intent and what it did, with its semantic changes.
        expect(eq(edit("\"first\"", "\"second\""), 0));
        const auto after_edit = invoke("design.history").second;
        const Value& last = after_edit.get("log")->list()->front();
        expect(last.get("message")->as_string() == "greetings — edit logic/side.stub" && !after_edit.get("uncommitted")->as_bool()) << after_edit.json();
        // Another branch: its texts run at once.
        expect(eq(invoke("design.history.switch", Value{}.set("to", "quiet")).first, 0));
        expect(eq(name_of(h, "side/hello"), std::string("first")));
        expect(eq(edit("\"bye\"}", "\"later\"}"), 0));
        expect(eq(invoke("design.history.switch", Value{}.set("to", "main")).first, 0));
        expect(eq(name_of(h, "side/hello"), std::string("second")) && eq(name_of(h, "side/bye"), std::string("bye")));
        // Merged line by line: each branch's own line.
        const auto [merged, merge_answer] = invoke("design.history.merge", Value{}.set("from", "quiet"));
        expect(eq(merged, 0)) << merge_answer.json();
        expect(eq(name_of(h, "side/hello"), std::string("second")) && eq(name_of(h, "side/bye"), std::string("later")));
        expect(merge_answer.get("commit") && merge_answer.get("commit")->get("parents")->list()->size() == 2) << merge_answer.json();
        const auto diff = invoke("design.history.diff", Value{}.set("from", "HEAD~1").set("to", "HEAD")).second;
        expect(diff.get("files") && diff.get("files")->list()->size() == 1 && diff.get("files")->list()->front().get("line")->as_int() == 3) << diff.json();
        expect(eq(invoke("design.history.tag", Value{}.set("name", "v1")).first, 0));
        // A commit seen, the branch staying where it is; then back to the beginning, kept as a new commit.
        const auto [seen, seen_answer] = invoke("design.history.switch", Value{}.set("to", "HEAD~1"));
        expect(eq(seen, 0) && seen_answer.get("detached") && seen_answer.get("detached")->as_bool()) << seen_answer.json();
        expect(eq(name_of(h, "side/bye"), std::string("bye")));
        const auto [reverted, revert_answer] = invoke("design.history.revert", Value{}.set("to", "HEAD~2"));
        expect(eq(reverted, 0)) << revert_answer.json();
        expect(eq(name_of(h, "side/hello"), std::string("first")) && eq(name_of(h, "side/bye"), std::string("bye")));
        const auto history = invoke("design.history").second;
        expect(eq(history.get("log")->list()->size(), std::size_t{4})) << history.json();
        expect(history.get("log")->list()->front().get("message")->as_string().starts_with("back to")) << history.json();
        expect(history.get("tags")->get("v1") != nullptr && !history.get("uncommitted")->as_bool()) << history.json();
        // Changes not committed (a file saved) are not dropped by a switch, unless asked.
        std::string saved = file_text(dir / "logic" / "side.stub");
        saved.replace(saved.find("\"first\""), 7, "\"draft\"");
        write(dir / "logic" / "side.stub", saved);
        expect(p.poll() == std::vector<std::string>{"logic/side.stub"});
        h.settle();
        expect(invoke("design.history.switch", Value{}.set("to", "quiet")).first != 0);
        expect(eq(invoke("design.history.switch", Value{}.set("to", "quiet").set("force", true)).first, 0));
        expect(eq(name_of(h, "side/bye"), std::string("later")));
        // The store beside the application.
        expect(std::filesystem::exists(dir / ".omui" / "design" / "HEAD") && std::filesystem::exists(dir / ".omui" / "design" / "refs" / "heads" / "quiet"));
    };

    "a preview and a variants board in stand-ins: their pictures and changes, the application untouched until one is taken"_test = [] {
        const auto dir = scratch_dir("player-stand-in");
        std::error_code error;
        std::filesystem::remove_all(dir / ".omui", error);
        const std::string doc = R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [
  {"type": "text", "id": "hello", "props": {"text": "first"}}]}}}
)j";
        write(dir / "logic" / "side.stub", doc);
        player::Manifest m;
        m.id = "org.openmui.player-stand-in";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        int stand_ins = 0;
        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true, .twin = stub_twins(m, &stand_ins)});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            h.settle();
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{answer.ndjson()}};
        };
        const auto saying = [&](std::string_view words) {
            std::string text = doc;
            text.replace(text.find("\"first\""), 7, std::format("\"{}\"", words));
            return text;
        };

        // A preview: the picture and the changes it would bring; nothing changed until accepted.
        const auto [previewed, preview] = invoke("design.preview", Value{}.set("path", "logic/side.stub").set("text", saying("previewed")));
        expect(eq(previewed, 0)) << preview.json();
        expect(eq(name_of(h, "side/hello"), std::string("first"))) << "the application untouched";
        expect(preview.json().contains("\"after\":\"previewed\"")) << preview.json();
        const auto picture = preview.get("picture") ? std::filesystem::path(std::string(preview.get("picture")->as_string())) : std::filesystem::path();
        expect(std::filesystem::exists(picture) && picture.extension() == ".png") << picture.string();
        expect(h.tree().node("design-preview/ghost").has_value()) << "shown over the application, half seen";
        expect(eq(invoke("design.preview.reject").first, 0));
        expect(!h.tree().node("design-preview/ghost").has_value() && eq(name_of(h, "side/hello"), std::string("first")));
        expect(eq(invoke("design.preview", Value{}.set("path", "logic/side.stub").set("text", saying("accepted"))).first, 0));
        const auto [accepted, accept_answer] = invoke("design.preview.accept");
        expect(eq(accepted, 0) && accept_answer.get("commit")) << accept_answer.json() << " — a step of the design";
        expect(eq(name_of(h, "side/hello"), std::string("accepted")));

        // A board of variants, each a branch with its picture; picking one runs it.
        Value::List variants;
        for (const std::string_view name : {"calm", "bold", "warm"})
            variants.push_back(Value{}.set("name", name).set("edits", Value(Value::List{Value{}.set("path", "logic/side.stub").set("text", saying(name))})));
        const auto [boarded, board] = invoke("design.variants", Value{}.set("brief", "how it greets").set("variants", Value(variants)));
        expect(eq(boarded, 0)) << board.json();
        expect(board.get("variants") && board.get("variants")->list()->size() == 3) << board.json();
        expect(eq(name_of(h, "side/hello"), std::string("accepted"))) << "nothing the person sees changed";
        expect(h.tree().node("design-variants/cards/variant-bold").has_value())
            << "the cards, side by side: " << h.tree().snapshot({.root = "design-variants"}).to_value().json();
        const auto history = invoke("design.history").second;
        expect(history.get("branches")->get("variant/calm") && history.get("branches")->get("variant/warm")) << history.json();
        const auto [picked, pick_answer] = invoke("design.variants.pick", Value{}.set("name", "bold"));
        expect(eq(picked, 0)) << pick_answer.json();
        expect(eq(name_of(h, "side/hello"), std::string("bold")) && !h.tree().node("design-variants/cards/variant-bold").has_value());
        expect(eq(stand_ins, 5)) << "two previews, three variants: a twin each";
        // Without twins, they say so.
        Headless plain(HeadlessOptions{.size = {320, 240}});
        player::Player without(plain.runtime(), m, {stub()}, {.design = true});
        expect(without.start().has_value());
        Invocation call;
        call.args = Value{}.set("path", "logic/side.stub").set("text", saying("x"));
        call.actor = {"agent", "designer", {}};
        const Envelope refused = plain.runtime().verbs().invoke("design.preview", call);
        expect(refused.exit_code().value_or(0) != 0 && refused.ndjson().contains("twin")) << refused.ndjson();
    };

    "the timeline: lanes of branches, commits in time with their pictures; a commit pressed is seen, steps go through time, two compared"_test = [] {
        const auto dir = scratch_dir("player-timeline");
        std::error_code error;
        std::filesystem::remove_all(dir / ".omui", error);
        const std::string doc = R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [
  {"type": "text", "id": "hello", "props": {"text": "one"}}]}}}
)j";
        write(dir / "logic" / "side.stub", doc);
        player::Manifest m;
        m.id = "org.openmui.player-timeline";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        int stand_ins = 0;
        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {480, 360}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true, .twin = stub_twins(m, &stand_ins)});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            h.settle();
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{answer.ndjson()}};
        };
        const auto say = [&](std::string_view words) {
            std::string text = doc;
            text.replace(text.find("\"one\""), 5, std::format("\"{}\"", words));
            return invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", text)).first;
        };
        // The person's press, as their pointer would make it.
        const auto press = [&](const std::string& path) {
            Invocation as;
            as.actor = {"person", "tester", {}};
            const Envelope done = semantic::Tree(h.runtime()).act(path, "activate", as);
            h.settle();
            return done.exit_code().value_or(-1);
        };
        expect(eq(invoke("design.begin", Value{}.set("intent", "counting")).first, 0));
        expect(eq(say("two"), 0) && eq(say("three"), 0));
        expect(eq(invoke("design.history.branch", Value{}.set("name", "spare").set("at", "HEAD~1")).first, 0));
        expect(eq(invoke("design.history.switch", Value{}.set("to", "spare")).first, 0));
        expect(eq(say("deux"), 0));
        expect(eq(invoke("design.history.switch", Value{}.set("to", "main")).first, 0));

        // Shown, with pictures made for the commits in stand-ins.
        const auto [opened, model] = invoke("design.timeline", Value{}.set("pictures", true));
        expect(eq(opened, 0)) << model.json();
        expect(model.get("lanes") && model.get("lanes")->list()->size() == 2 && model.get("columns")->as_int() == 4) << model.json();
        expect(model.get("lanes")->list()->front().get("branch")->as_string() == "main") << "the branch designed on first";
        expect(eq(stand_ins, 4)) << "a picture for each commit, a twin each";
        expect(model.json().contains(".png")) << model.json();
        expect(h.tree().node("design-timeline/panel/bar/back").has_value()) << "shown over the application";
        // A commit pressed: the application shows it, the branch where it was.
        const auto& main_commits = *model.get("lanes")->list()->front().get("commits")->list();
        const std::string first(main_commits.front().get("id")->as_string());
        const std::string cell = std::format("design-timeline/panel/lanes/branches/lane-main/commits/commit-{}", first.substr(0, 12));
        expect(h.tree().node(cell).has_value()) << cell;
        expect(eq(press(cell), 0));
        expect(eq(name_of(h, "side/hello"), std::string("one"))) << "the first commit, seen";
        // Forward in time, then back.
        expect(eq(invoke("design.timeline", Value{}.set("step", 1)).first, 0));
        expect(eq(name_of(h, "side/hello"), std::string("two")));
        expect(eq(invoke("design.timeline", Value{}.set("step", 2)).first, 0));
        expect(eq(name_of(h, "side/hello"), std::string("deux"))) << "time, across branches";
        expect(eq(invoke("design.timeline", Value{}.set("at", "main")).first, 0));
        expect(eq(name_of(h, "side/hello"), std::string("three")));
        // Two compared: the person presses Compare, then two commits.
        expect(eq(press("design-timeline/panel/bar/compare"), 0));
        const std::string last(main_commits.back().get("id")->as_string());
        for (const auto& id : {first, last}) expect(eq(press(std::format("design-timeline/panel/lanes/branches/lane-main/commits/commit-{}", id.substr(0, 12))), 0));
        const auto note = h.tree().node("design-timeline/panel/note");
        expect(note.has_value() && note->name.contains("logic/side.stub from line 2")) << (note ? note->name : std::string("<none>"));
        expect(eq(name_of(h, "side/hello"), std::string("three"))) << "comparing changes nothing";
        // Put away.
        expect(eq(invoke("design.timeline", Value{}.set("show", false)).first, 0));
        expect(!h.tree().node("design-timeline/panel/bar/back").has_value());
    };

    "the design as a film: each step run in a twin as it was presented, under its caption, frame by frame; a description for omui video"_test = [] {
        const auto dir = scratch_dir("designer-film");
        std::error_code error;
        std::filesystem::remove_all(dir / ".omui", error);
        const std::string doc = R"j({"omui": "0.1", "view": {"root": {"type": "column", "children": [
  {"type": "text", "id": "hello", "props": {"text": "one"}}]}}}
)j";
        write(dir / "logic" / "side.stub", doc);
        player::Manifest m;
        m.id = "org.openmui.designer-film";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true, .twin = stub_twins(m)});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            h.settle();
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{answer.ndjson()}};
        };
        expect(eq(invoke("design.begin", Value{}.set("intent", "a film")).first, 0));
        for (const std::string_view words : {"two", "three"}) {
            std::string text = doc;
            text.replace(text.find("\"one\""), 5, std::format("\"{}\"", words));
            expect(eq(invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", text)).first, 0));
        }
        const auto [filmed, film] = invoke("design.film", Value{}.set("fps", 10).set("seconds", 0.3));
        expect(eq(filmed, 0)) << film.json();
        expect(film.get("steps") && film.get("steps")->as_int() == 3 && film.get("frames")->as_int() == 9) << film.json();
        const auto out = dir / ".omui" / "design" / "film" / "main";
        const std::string description = file_text(out / "design.video.omui.json");
        expect(description.contains("\"frames\": 9") && description.contains("\"png\": \"frames/{:05}.png\"") && description.contains("\"width\": 320")) << description;
        const std::string first = file_text(out / "frames" / "00000.png"), last = file_text(out / "frames" / "00008.png");
        expect(!first.empty() && !last.empty() && first != last) << "the design changing over the film";
        expect(eq(name_of(h, "side/hello"), std::string("three"))) << "what the person sees untouched";
    };

    "a motion expected and measured in a twin: how long it took, how far it sprang past — a fact against the person's words; fixed, it holds"_test = [] {
        const auto dir = scratch_dir("designer-motion");
        std::error_code error;
        std::filesystem::remove_all(dir / ".omui", error);
        const std::string springy = R"j({"omui": "0.1", "view": {"signals": {"open": {"type": "bool"}},
  "root": {"type": "column", "children": [
  {"type": "button", "id": "toggle", "props": {"text": "Plans"}, "on": {"activated": [{"set": "open", "expr": "!open"}]}},
  {"type": "box", "id": "card", "props": {"width": 120, "height": {"expr": "open ? 160 : 40"}, "background": "#38bdf8"},
   "animate": {"height": {"spring": {"stiffness": 300, "damping": 10}}}}]}}}
)j";
        write(dir / "logic" / "side.stub", springy);
        player::Manifest m;
        m.id = "org.openmui.designer-motion";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.level = HeadlessLevel::pixels, .size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true, .twin = stub_twins(m)});
        expect(p.start().has_value());
        h.settle();
        const auto invoke = [&](std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            const Envelope answer = h.runtime().verbs().invoke(verb, call);
            h.settle();
            return std::pair{answer.exit_code().value_or(-1), answer.data_value() ? *answer.data_value() : Value{answer.ndjson()}};
        };
        const auto motion = [](double overshoot) {
            return Value{}
                .set("act", Value{}.set("node", "side/toggle").set("action", "activate"))
                .set("node", "side/card")
                .set("measure", "height")
                .set("within_ms", Value(Value::List{Value(50), Value(2500)}))
                .set("overshoot", overshoot);
        };
        expect(eq(invoke("design.expect", Value{}.set("say", "the card opens, a lively spring").set("motion", motion(0.6))).first, 0));
        expect(eq(invoke("design.expect", Value{}.set("say", "the card opens without springing past").set("motion", motion(0.05))).first, 0));
        const auto [checked, facts] = invoke("design.check");
        expect(eq(checked, 0)) << facts.json();
        const auto& results = *facts.get("results")->list();
        expect(results.size() == 2 && results[0].get("holds")->as_bool() && !results[1].get("holds")->as_bool()) << facts.json();
        const Value* measured = results[1].get("measured");
        expect(measured && measured->get("from")->as_double() < 41 && measured->get("to")->as_double() > 159) << facts.json();
        expect(measured && measured->get("overshoot")->as_double() > 0.2 && measured->get("settle_ms")->as_double() > 100) << facts.json();
        expect(results[1].get("why")->as_string().contains("more than 5%")) << results[1].get("why")->as_string();
        expect(eq(name_of(h, "side/toggle"), std::string("Plans"))) << "measured in a twin: the person's application untouched";
        // The fix the agent proposes — more damping — and the facts again.
        std::string calm = springy;
        const std::string_view lively = "\"damping\": 10";
        calm.replace(calm.find(lively), lively.size(), "\"damping\": 40");
        expect(eq(invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", calm)).first, 0));
        const auto again = invoke("design.check").second;
        expect(again.get("held") && again.get("held")->as_int() == 2) << again.json();
    };

    "a merge of two texts by lines: each side's change, the same change once, overlapping changes a conflict"_test = [] {
        const std::string base = "a\nb\nc\nd";
        const auto both = designer::merge_text(base, "A\nb\nc\nd", "a\nb\nc\nD");
        expect(!both.conflict && both.text == "A\nb\nc\nD") << both.text;
        const auto same = designer::merge_text(base, "a\nB\nc\nd", "a\nB\nc\nd");
        expect(!same.conflict && same.text == "a\nB\nc\nd");
        const auto inserted = designer::merge_text(base, "a\nb\nx\nc\nd", "a\nb\nc\nd\ny");
        expect(!inserted.conflict && inserted.text == "a\nb\nx\nc\nd\ny") << inserted.text;
        expect(designer::merge_text(base, "a\nX\nc\nd", "a\nY\nc\nd").conflict);
    };

    "a module a script imports: watched as the script is; edited and committed by design.edit like a script"_test = [] {
        const auto dir = scratch_dir("player-modules");
        const std::string one = R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "one"}}}})j";
        write(dir / "logic" / "main.stub", "the entry");
        write(dir / "logic" / "part.stub", one);
        ModuleLog log;
        const auto part = dir / "logic" / "part.stub";
        player::Language language{"stub", {".stub"}, [&](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<ModuleStub>(rt, part, log); }, {}, {}};
        player::Manifest m;
        m.id = "org.openmui.player-modules";
        m.base = dir;
        m.scripts = {{"stub", "logic/main.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {language}, {.design = true});
        expect(p.start().has_value());
        h.settle();
        expect(eq(name_of(h, "side"), std::string("one")));
        (void)p.poll();  // the module seen once
        // Saved on disk: its script runs again, told which module changed.
        write(part, R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "two"}}}})j");
        const auto reloaded = p.poll();
        expect(std::ranges::find(reloaded, std::string("logic/part.stub")) != reloaded.end()) << std::format("{} reloaded", reloaded.size());
        h.settle();
        expect(eq(name_of(h, "side"), std::string("two")));
        expect(!log.changes.empty() && log.changes.back() == "part.stub=(file)");
        // design.sources lists it; design.edit runs new text for it.
        Invocation call;
        call.actor = {"agent", "designer", {}};
        call.args = Value(Value::Map{});
        const auto sources = h.runtime().verbs().invoke("design.sources", call);
        expect(sources.ndjson().contains("\"imported_by\":\"logic/main.stub\"")) << sources.ndjson();
        call.args = Value{}.set("path", "logic/part.stub").set("text", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "three"}}}})j");
        const auto edited = h.runtime().verbs().invoke("design.edit", call);
        expect(eq(edited.exit_code().value_or(-1), 0)) << edited.ndjson();
        expect(eq(name_of(h, "side"), std::string("three")));
        expect(edited.ndjson().contains("\"after\":\"three\"")) << edited.ndjson();
        // Committed: written to the module's file; the watcher does not run it again.
        call.args = Value(Value::Map{});
        const auto committed = h.runtime().verbs().invoke("design.commit", call);
        expect(eq(committed.exit_code().value_or(-1), 0)) << committed.ndjson();
        expect(file_text(part).contains("three"));
        const int runs = log.runs;
        expect(p.poll().empty() && log.runs == runs) << "committed text is not run twice";
    };

    "a design edit, seen over time: what it adds is written in — a middle state, then the same end as without the presentation"_test = [] {
        const auto page = [](bool cards) {
            std::string children = R"j({"type": "text", "id": "title", "props": {"text": "Subscriptions"}})j";
            if (cards)
                for (const auto* plan : {"Monthly", "Yearly"})
                    children += std::format(R"j(, {{"type": "box", "id": "{}", "props": {{"width": 120, "height": 44, "background": "#2563eb", "radius": 10}}}})j", plan);
            return std::format(R"j({{"omui": "0.1", "view": {{"root": {{"type": "column", "id": "page", "props": {{"padding": 16, "gap": 10}}, "children": [{}]}}}}}})j", children);
        };
        const auto dir = scratch_dir("player-design-film");
        write(dir / "logic" / "side.stub", page(false));
        player::Manifest m;
        m.id = "org.openmui.player-design-film";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {240, 200}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        expect(p.start().has_value());
        h.settle();
        Invocation call;
        call.actor = {"agent", "designer", {}};
        call.args = Value{}.set("path", "logic/side.stub").set("text", page(true));
        expect(eq(h.runtime().verbs().invoke("design.edit", call).exit_code().value_or(-1), 0));
        // Nine moments over 0.8 s, side by side.
        std::vector<xrender::raster::Surface> frames;
        for (int index = 0; index < 9; ++index) {
            if (index) (void)h.advance(seconds(0.1));
            else (void)h.step();
            frames.push_back(h.pixels());
        }
        const auto differ = [](const xrender::raster::Surface& a, const xrender::raster::Surface& b) {
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x)
                    if (!(a.at(x, y) == b.at(x, y))) return true;
            return false;
        };
        expect(differ(frames[2], frames.front()) && differ(frames[2], frames.back())) << "a real middle state";
        // The end is the edit's own drawing: the same page mounted with no presentation.
        Headless plain(HeadlessOptions{.size = {240, 200}});
        const std::string end = page(true);
        expect(abi::omuiMount(abi::attach(plain.runtime(), "plain"), {"side", 4}, {end.data(), end.size()}) == abi::OMUI_SUCCESS);
        plain.settle();
        // The code it wrote stays beside it for a moment (design-code); after it, the edit's own drawing.
        (void)h.advance(seconds(3.0));
        expect(!differ(h.pixels(), plain.pixels())) << "the presentation ends where the edit draws";
        // The strip, for the eye (README media are made from it).
        const int width = frames.front().width(), height = frames.front().height();
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(width) * frames.size() * height * 4);
        for (std::size_t f = 0; f < frames.size(); ++f)
            for (int y = 0; y < height; ++y)
                std::memcpy(strip.data() + (static_cast<std::size_t>(y) * width * frames.size() + f * width) * 4, frames[f].row(y), static_cast<std::size_t>(width) * 4);
        const mlibs::image::View view{strip.data(), static_cast<int>(width * frames.size()), height, static_cast<int>(width * frames.size() * 4)};
        auto png = mlibs::image::encode_png(view);
        expect(png.has_value());
        if (png) std::ofstream(dir / "design-edit.strip.png", std::ios::binary).write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
        std::println("design edit strip: {}", (dir / "design-edit.strip.png").string());
    };

    "a demonstration as a test: the person's steps after design.handover saved as a scenario with what is expected, replayed on a fresh start"_test = [] {
        const auto dir = scratch_dir("player-scenario");
        write(dir / "ui" / "main.view.omui.json", counter);
        write(dir / "ui" / "badge.view.omui.json", badge_doc);
        player::Manifest m;
        m.id = "org.openmui.player-scenario";
        m.base = dir;
        m.components = {"ui/badge.view.omui.json"};
        m.views = {"ui/main.view.omui.json"};
        const auto as_agent = [](Headless& h, std::string_view verb, Value args = Value(Value::Map{})) {
            Invocation call;
            call.args = std::move(args);
            call.actor = {"agent", "designer", {}};
            return h.runtime().verbs().invoke(verb, call);
        };
        {
            Headless h(HeadlessOptions{.size = {320, 240}});
            player::Player p(h.runtime(), m, {stub()}, {.design = true});
            expect(p.start().has_value());
            h.settle();
            expect(eq(as_agent(h, "design.handover").exit_code().value_or(-1), 0));
            Invocation person;  // the person, at the machine
            expect(eq(h.tree().act("main/inc", "activate", person).exit_code().value_or(-1), 0));
            h.settle();
            expect(eq(name_of(h, "main/n"), std::string("1 clicks")));
            (void)as_agent(h, "design.expect", Value{}.set("say", "one click counted").set("query", Value{}.set("id", "main/n").set("name", "1 clicks")));
            const auto saved = as_agent(h, "design.save_scenario", Value{}.set("name", "one-click"));
            expect(eq(saved.exit_code().value_or(-1), 0) && saved.ndjson().contains("\"steps\":1")) << saved.ndjson();
        }
        expect(file_text(dir / "tests" / "one-click.scenario.omui.json").contains("\"node\":\"main/inc\""));
        // A fresh start: the count is 0 again, and the scenario brings it to what was expected.
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()}, {.design = true});
        expect(p.start().has_value());
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("0 clicks")));
        const auto replayed = as_agent(h, "design.replay", Value{}.set("name", "one-click"));
        expect(eq(replayed.exit_code().value_or(-1), 0) && replayed.ndjson().contains("\"held\":1") && replayed.ndjson().contains("\"of\":1")) << replayed.ndjson();
    };
}};
