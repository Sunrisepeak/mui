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
using mlibs::cts::Status;

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

UTest cases{"agent.interface-subject", [] {
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
        const auto report = contract::run(probe, {.subject = "omui.agent"});
        std::println("{}", report.text());
        expect(report.ok()) << report.text();
        std::vector<std::string> unsupported;
        for (const auto& outcome : report.outcomes)
            if (outcome.status == Status::unsupported) unsupported.push_back(outcome.id);
        expect(unsupported == std::vector<std::string>{"agent.time.replays", "agent.source.chain", "agent.observe.budget"})
            << "no host draws here (a pixel's source is the host's); a budget of bytes and replayed time are not its";
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
