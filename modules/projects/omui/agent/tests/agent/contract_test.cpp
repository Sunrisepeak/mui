// The interface as a subject of the agent kernel (D-173, D-174): found in a
// kernel session beside whatever else a process holds, its operations the
// protocol's own by their catalog names, and held to the contract every
// subject is — the eight native properties, on a small settings page.
import std;
import openas;
import omui.agent;
import omui.ui.base;
import mlibs.agent.cts;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;
namespace contract = mlibs::agent::cts;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection made;
        (void)made.add_defaults();
        return made;
    }();
    return collection;
}

// A settings page running, and the protocol an endpoint would serve.
struct Page {
    Runtime                        rt;
    std::optional<agent::Protocol> protocol;
    int                            exported = 0;
    Page()
        : rt([] {
              RuntimeOptions options;
              options.fonts = &fonts();
              options.size = {400, 300};
              return options;
          }()) {
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development, .source = "contract-test"});
        rt.set_agent_access(std::move(access));
        (void)rt.verbs().add({"settings.export", "Write the settings to a file a person can share", Effect::reversible, "", {{"path", "string"}}}, [this](const Value&) -> VerbResult {
            ++exported;
            return Value{};
        });
        // What an agent new to it can get done here, said by the application: a verb, then a node's action.
        Value where;
        where.set("path", "settings.json");
        (void)rt.verbs().add_task({"export-with-the-dark-look", "Turn the dark look on and write the settings to a file a person can share",
                                   {{.node = "main/dark", .action = "toggle"}, {.verb = "settings.export", .args = where}}});
        rt.mount("main", column({.padding = 8, .gap = 8}, {text_box({.id = "name", .label = "Name"}), toggle({.id = "dark", .label = "Dark"}),
                                                           slider({.id = "volume", .max = 100.0, .value = 20.0})}));
        (void)rt.frame({seconds(0), {}});
        protocol.emplace(rt, agent::ProtocolOptions{.app = "settings", .default_actor = "agent:nobody"});
    }
};
// The page first (a base before the subject that reads it), then the interface as a subject over it.
struct Shown final : Page, agent::InterfaceSubject {
    Shown() : agent::InterfaceSubject(rt, *protocol) {}
};

struct Answered {
    std::vector<mlibs::json::Document> records;
    [[nodiscard]] mlibs::json::Node data() const {
        for (const auto& record : records)
            if (record.root()["kind"].text() == "data") return record.root()["data"];
        return {};
    }
    [[nodiscard]] std::string error() const {
        for (const auto& record : records)
            if (record.root()["kind"].text() == "error") return record.root()["code"].text().value_or("");
        return {};
    }
    [[nodiscard]] std::int64_t exit_code() const { return records.empty() ? -1 : records.back().root()["exitCode"].integer().value_or(-1); }
};
Answered ask(mlibs::agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        out.records.push_back(std::move(*parsed));
    });
    return out;
}

}  // namespace

static UTest cases{"agent.interface-subject", [] {
    "the agent contract on the interface: what it has passes, what it says it has not is unsupported"_test = [] {
        register_base_components();
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<mlibs::agent::Subject> { return std::make_unique<Shown>(); },
            .read = R"({"op":"ui.snapshot"})",
            .change = R"({"op":"ui.act","node":"main/name","action":"set_value","args":{"value":"Ada"}})",
            .undo = R"({"op":"ui.undo"})",
            .preview = R"({"op":"ui.preview","steps":[{"node":"main/dark","action":"toggle"}],"pictures":false})",
            .explain = R"({"op":"ui.timeline"})",
        };
        contract::declare(probe, {.subject = "omui.agent"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.time.replays", "agent.source.chain", "agent.observe.budget"})
            << "no host draws here (a pixel's source is the host's); a budget of bytes and replayed time are not its";
    };

    "what the application names is found by intent: its verbs and its nodes, each with a request to send as it is (D-186)"_test = [] {
        register_base_components();
        Shown shown;
        mlibs::agent::SubjectSession session({&shown}, {.actor = {openas::Kind::agent, "reviewer", {}}});
        const auto verb = ask(session, R"({"op":"find","q":"share the settings as a file"})");
        const auto first = verb.data()["found"][0];
        expect(first["kind"].text() == "verb" && first["name"].text() == "settings.export" && first["summary"].text().value_or("").contains("takes path: string")) << verb.data().dump();
        // Its request tries it — a dry run: what it would do, nothing done.
        expect(first["requests"][0].dump() == R"({"op":"ui.invoke","verb":"settings.export","dry_run":true})");
        expect(eq(ask(session, first["requests"][0].dump()).exit_code(), std::int64_t{0}) && eq(shown.exported, 0));
        const auto node = ask(session, R"({"op":"find","q":"the dark toggle"})");
        const auto found = node.data()["found"][0];
        expect(found["kind"].text() == "node" && found["name"].text() == "main/dark" && found["summary"].text().value_or("").contains("toggle")) << node.data().dump();
        const auto read = ask(session, found["requests"][0].dump());
        expect(eq(read.exit_code(), std::int64_t{0}) && read.data().dump().contains("main/dark")) << "its request reads the node";
    };

    "the application's own tasks to begin with: described first, found by intent, run as written (D-186)"_test = [] {
        register_base_components();
        Shown shown;
        mlibs::agent::SubjectSession session({&shown}, {.actor = {openas::Kind::agent, "reviewer", {}}});
        const auto described = ask(session, R"({"op":"describe","subject":"ui"})");
        const auto first = described.data()["tasks"][0];
        expect(first["name"].text() == "export-with-the-dark-look") << "the application's before the interface's: " << described.data()["tasks"].dump();
        expect(described.data()["tasks"].size() > std::size_t{1}) << "the interface's own tasks are still there";
        expect(first["steps"][0].dump() == R"({"op":"ui.act","node":"main/dark","action":"toggle"})" &&
               first["steps"][1].dump() == R"({"op":"ui.invoke","verb":"settings.export","args":{"path":"settings.json"}})")
            << first.dump();
        // By what it is for, in other words than its name.
        const auto found = ask(session, R"({"op":"find","q":"write the settings so a person can share them, dark"})");
        expect(found.data()["found"][0]["kind"].text() == "task" && found.data()["found"][0]["name"].text() == "export-with-the-dark-look") << found.data().dump();
        // Its requests, sent as they are, do it.
        for (std::size_t index = 0; index < first["steps"].size(); ++index) expect(eq(ask(session, first["steps"][index].dump()).exit_code(), std::int64_t{0}));
        expect(eq(shown.exported, 1));
        const auto dark = ask(session, R"({"op":"ui.snapshot","root":"main/dark","depth":0})");
        expect(dark.data()["nodes"][0]["value"].dump() == "true") << dark.data().dump();
    };

    "at an application's endpoint the kernel's own requests are reached: `log`, `find` in words, `help` — and the protocol's `find` is still its own"_test = [] {
        register_base_components();
        Page page;
        // Two connections of one endpoint: a session each, one book of what was asked.
        const auto book = std::make_shared<mlibs::agent::Facts>();
        agent::ProcessSession first(page.rt, agent::ProtocolOptions{.app = "settings", .default_actor = "agent:reviewer"});
        agent::ProcessSession second(page.rt, agent::ProtocolOptions{.app = "settings", .default_actor = "agent:reviewer"});
        first.keep_facts_in(book), second.keep_facts_in(book);
        const auto ask_of = [](agent::ProcessSession& session, std::string_view line) {
            Answered out;
            session.request(line, [&](std::string_view record) {
                auto parsed = mlibs::json::Document::parse(record, "record");
                expect(fatal(parsed.has_value())) << record;
                out.records.push_back(std::move(*parsed));
            });
            return out;
        };
        expect(ask_of(first, R"({"op":"help"})").data()["subjects"][0]["name"].text() == "ui") << "asked first by one who knows nothing";
        expect(eq(ask_of(first, R"({"op":"ui.act","node":"main/dark","action":"toggle"})").exit_code(), std::int64_t{0}));
        // In words: the kernel's, across what the application names. As an object: the protocol's, over the tree.
        const auto wanted = ask_of(second, R"({"op":"find","q":"share the settings as a file"})");
        expect(wanted.data()["found"][0]["name"].text() == "settings.export" || wanted.data()["found"][0]["name"].text() == "export-with-the-dark-look") << wanted.data().dump();
        const auto queried = ask_of(second, R"({"op":"find","q":{"role":"switch"}})");
        expect(queried.error().empty() && queried.data().dump().contains("main/dark")) << queried.data().dump();
        // What the first connection did, read by the second.
        const auto logged = ask_of(second, R"({"op":"log"})");
        expect(logged.error().empty() && logged.data().dump().contains("ui.act") && logged.data().dump().contains("main/dark")) << logged.data().dump();
    };

    "found in a kernel session: who is here, then the protocol's own operations by their catalog names, answered in its own records"_test = [] {
        register_base_components();
        Shown shown;
        mlibs::agent::SubjectSession session({&shown}, {.actor = {openas::Kind::agent, "reviewer", {}}});
        const auto who = ask(session, R"({"op":"subjects"})");
        expect(who.data()["subjects"][0]["name"].text() == "ui" && who.data()["subjects"][0]["interface"].text() == "openmui.tree@0.1");
        expect(eq(ask(session, R"({"op":"describe","subject":"ui"})").data()["operations"].size(), std::size_t{32}));
        expect(ask(session, R"({"op":"describe","operation":"ui.describe"})").data()["name"].text() == "ui.describe")
            << "the interface's own `describe` is an operation like the others: the session's has no dot";

        const auto found = ask(session, R"({"op":"ui.find","q":{"role":"text_box"},"re":7})");
        expect(eq(found.exit_code(), std::int64_t{0}) && found.records[0].root()["re"].integer() == 7) << "the kernel's answer echoes `re`";
        const auto typed = ask(session, R"({"op":"ui.act","node":"main/name","action":"set_value","args":{"value":"Ada"},"actor":"person:mallory"})");
        expect(eq(typed.exit_code(), std::int64_t{0}));
        const auto state = ask(session, R"({"op":"ui.state"})");
        expect(state.data().dump().contains("Ada")) << state.data().dump();
        // Who did it is the session's to say: the record names the reviewer, not who the line claimed.
        const std::string timeline = ask(session, R"({"op":"ui.timeline"})").data().dump();
        expect(timeline.contains("agent:reviewer") && !timeline.contains("mallory")) << timeline;

        expect(ask(session, R"({"op":"ui.act","node":"main/nowhere","action":"activate"})").error() == "not-found") << "the protocol's own errors, as they are";
        expect(ask(session, R"({"op":"ui.fly"})").error() == "not-found");
    };
}};
