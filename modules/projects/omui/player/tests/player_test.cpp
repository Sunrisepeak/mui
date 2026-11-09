// The player: an application's components, views and scripts started from a
// manifest; files that change on disk reloaded in place, state kept; errors
// kept with their places while the old view stays.
import std;
import omui.core;
import omui.view;
import omui.abi;
import omui.player;
import omui.ui.base;
import omui.host.headless;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
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

player::Language stub() {
    return {"stub", {".stub"}, [](abi::OmuiRuntime rt) -> std::unique_ptr<player::Engine> { return std::make_unique<Stub>(rt); }, {}};
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

UTest cases{"player", [] {
    register_base_components();

    "a manifest's components, views and scripts start; saved files reload in place"_test = [] {
        const auto dir = scratch_dir("player");
        write(dir / "ui" / "main.view.omui.json", counter);
        write(dir / "ui" / "badge.view.omui.json", badge_doc);
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "id": "hello", "props": {"text": "from a script"}}}})j");
        player::Manifest m;
        m.id = "org.openmui.player-test";
        m.base = dir;
        m.components = {"ui/badge.view.omui.json"};
        m.views = {"ui/main.view.omui.json"};
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        auto started = p.start();
        expect(started.has_value()) << (started ? "" : started.error());
        h.settle();
        expect(p.idle());
        expect(eq(name_of(h, "main/badge/t"), std::string("CLICKS")));
        expect(eq(name_of(h, "side"), std::string("from a script")));
        // (The script's view is mounted over the main one: an agent's action reaches the button.)
        Invocation call;
        call.actor = {"agent", "test", {}};
        const auto e = h.tree().act("main/inc", "activate", call);
        expect(eq(e.exit_code().value_or(-1), 0));
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 clicks")));

        // A view saved: in place, its state kept.
        std::string next(counter);
        next.replace(next.find("{} clicks"), 9, "{} taps");
        write(dir / "ui" / "main.view.omui.json", next);
        expect(p.poll() == std::vector<std::string>{"ui/main.view.omui.json"});
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 taps")));
        // A component saved: its instances rebuilt.
        std::string loud(badge_doc);
        loud.replace(loud.find("upper(label)"), 12, "format('<{}>', label)");
        write(dir / "ui" / "badge.view.omui.json", loud);
        expect(p.poll() == std::vector<std::string>{"ui/badge.view.omui.json"});
        h.settle();
        expect(eq(name_of(h, "main/badge/t"), std::string("<clicks>")));
        // A script saved: run again.
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "again"}}}})j");
        expect(p.poll() == std::vector<std::string>{"logic/side.stub"});
        h.settle();
        expect(eq(name_of(h, "side"), std::string("again")));
        // A view saved with a mistake: nothing changes, the problem says where.
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "colum"}}})j");
        expect(p.poll().empty());
        expect(!p.problems().empty() && p.problems().back().find("ui/main.view.omui.json:1:43: error: no component is named `colum`") != std::string::npos) << (p.problems().empty() ? "" : p.problems().back());
        h.settle();
        expect(eq(name_of(h, "main/n"), std::string("1 taps")));
    };

    "watch reloads on the runtime's timers"_test = [] {
        const auto dir = scratch_dir("player-watch");
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "one"}}}})j");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/main.view.omui.json"};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {});
        expect(p.start().has_value());
        p.watch(seconds(0.1));
        h.settle(seconds(0.05));
        expect(eq(name_of(h, "main"), std::string("one")));
        write(dir / "ui" / "main.view.omui.json", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "two"}}}})j");
        (void)h.advance(seconds(0.2));
        expect(eq(name_of(h, "main"), std::string("two")));
    };

    "what cannot start is a problem, not a crash"_test = [] {
        const auto dir = scratch_dir("player-problems");
        player::Manifest m;
        m.base = dir;
        m.views = {"ui/missing.view.omui.json"};
        m.scripts = {{"cobol", "logic/main.cbl"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        auto r = p.start();
        expect(!r);
        expect(r.error().find("ui/missing.view.omui.json: cannot be read") != std::string::npos) << r.error();
        expect(r.error().find("logic/main.cbl: no language `cobol`") != std::string::npos) << r.error();
    };

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
        expect(eq(file_text(dir / "logic" / "side.stub"), first)) << "an edit stays in the session until committed";

        // A text that does not run: refused with its problem, nothing changed.
        const auto refused = invoke("design.edit", Value{}.set("path", "logic/side.stub").set("text", "{\"omui\": \"0.1\", \"view\": {\"root\": {\"type\": \"colum\"}}}"));
        expect(refused.exit_code().value_or(0) != 0) << refused.ndjson();
        h.settle();
        expect(eq(name_of(h, "side"), std::string("second"))) << "what ran before runs";
        expect(invoke("design.edit", Value{}.set("path", "logic/nothing.stub").set("text", first)).exit_code().value_or(0) != 0);

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

    "out of development mode there are no design verbs"_test = [] {
        const auto dir = scratch_dir("player-no-design");
        write(dir / "logic" / "side.stub", R"j({"omui": "0.1", "view": {"root": {"type": "text", "props": {"text": "x"}}}})j");
        player::Manifest m;
        m.id = "org.openmui.player-no-design";
        m.base = dir;
        m.scripts = {{"stub", "logic/side.stub"}};
        Headless h(HeadlessOptions{.size = {320, 240}});
        player::Player p(h.runtime(), m, {stub()});
        expect(p.start().has_value());
        expect(h.runtime().verbs().find("design.edit") == nullptr);
    };
}};
