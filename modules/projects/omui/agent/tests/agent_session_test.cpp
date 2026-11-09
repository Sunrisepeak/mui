import std;
import omui.agent;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::recent_logs;
using mlibs::log::set_log_filter;
}

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
int exit_of(const std::vector<Value>& reply) {
    return reply.empty() || !reply.back().get("exitCode") ? -100 : static_cast<int>(reply.back().get("exitCode")->as_int());
}
std::vector<Value> talk(agent::Protocol& p, std::string_view request) {
    std::vector<Value> reply;
    p.request(request, [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
    return reply;
}
struct Fixture {
    std::shared_ptr<AgentAccess> access = std::make_shared<AgentAccess>();
    Runtime rt{RuntimeOptions{.size = {240, 160}, .stages = Stages::layout, .access = access}};
    int sent = 0, person = 0;
    Fixture() {
        access->set_policy({.mode = AgentMode::development});
        expect(rt.verbs()
                  .add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
                       [this](const Value&) -> VerbResult {
                           ++sent;
                           return Value{};
                       })
                  .has_value());
        expect(rt.verbs()
                  .add({.name = "person.confirm", .effect = Effect::read, .person = true},
                       [this](const Value&) -> VerbResult {
                           ++person;
                           return Value{};
                       })
                  .has_value());
        rt.mount(
            "main",
            column({},
                   {
                       box({.id = "send", .width = 100, .height = 30, .label = "Send"}).prop("verb", Source{Value("mail.send"), {}, {}}),
                   }));
        (void)rt.frame({seconds(0), {}});
    }
};
} // namespace

UTest cases{"agent.session", [] {
    "Protocol callbacks preserve the narrowed session across both invocation entries"_test = [] {
        Fixture f;
        Envelope nested, raw, person;
        expect(f.rt.verbs()
                  .add({.name = "wrapper.read", .effect = Effect::read},
                       [&](const Value&) -> VerbResult {
                           nested = f.rt.invoke("mail.send", {.answer = "send"});
                           raw = f.rt.verbs().invoke("mail.send", {.answer = "send"});
                           person = f.rt.invoke("person.confirm");
                           return Value{};
                       })
                  .has_value());
        AgentRestrictions limits;
        limits.cap = openas::Cap{openas::Effect::read};
        agent::Protocol p(f.rt, {.session_limits = limits});
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"wrapper.read","actor":"agent:planner"})")) == 0);
        expect(nested.exit_code() == 1 && raw.exit_code() == 1 && person.exit_code() == 1);
        expect(f.sent == 0 && f.person == 0);
        const auto journal = f.rt.verbs().journal();
        expect(journal.size() == 4);
        for (const auto& entry : journal)
            expect(entry.actor.starts_with("agent:planner;via=service:development-session-")) << entry.actor;
    };

    "a trusted session cannot cross an instance or survive explicit revocation"_test = [] {
        Fixture f, another;
        auto session = f.access->open_session();
        agent::Protocol wrong(another.rt, {.session = session});
        expect(exit_of(talk(wrong, R"({"op":"invoke","verb":"mail.send","answer":"send"})")) == 1);
        expect(another.sent == 0);
        agent::Protocol p(f.rt, {.session = session});
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 0);
        expect(f.access->revoke_session(*session));
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"snapshot"})")) == 1);
        const auto d = talk(p, R"({"op":"describe"})");
        expect(exit_of(d) == 0 && !d.front().get("data")->get("agent_policy")->get("session_valid")->as_bool());
        expect(f.sent == 1);
    };

    "real input frames carry a development grant and preserve narrowing at action time"_test = [] {
        Fixture f;
        auto session = f.access->open_session();
        agent::Protocol p(f.rt, {.input = true, .session = session});
        std::vector<Value> reply;
        p.request(R"({"op":"input","actor":"agent:keyboard","events":[{"type":"click","node":"main/send"}]})",
                  [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        int frames = 0;
        while (auto batch = p.take_input()) {
            expect(batch->session == session);
            f.rt.set_input_actor(batch->actor, batch->session);
            (void)f.rt.frame({seconds(++frames * 0.02), std::move(batch->events)});
            f.rt.set_input_actor(std::nullopt);
            p.poll();
            if (frames == 8) break;
        }
        expect(frames > 0 && frames < 8 && exit_of(reply) == 0);
        expect(f.sent == 1) << "the real click invoked the external verb through its trusted session";
        expect(f.rt.verbs().journal().back().actor.find(session->owner()) != std::string::npos);
        expect(!f.rt.state(f.rt.find("main/send")).pressed);
    };

    "revoking a queued session cancels a prior press without synthesizing activation"_test = [] {
        Fixture f;
        auto session = f.access->open_session();
        agent::Protocol p(f.rt, {.input = true, .session = session});
        std::vector<Value> reply;
        p.request(R"({"op":"input","events":[{"type":"click","node":"main/send"}]})",
                  [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        auto down = p.take_input();
        expect(down.has_value());
        if (!down) return;
        f.rt.set_input_actor(down->actor, down->session);
        (void)f.rt.frame({seconds(0.02), std::move(down->events)});
        f.rt.set_input_actor(std::nullopt);
        expect(f.rt.state(f.rt.find("main/send")).pressed);
        expect(f.access->revoke_session(*session));
        auto cleanup = p.take_input();
        expect(cleanup.has_value());
        if (cleanup) {
            expect(cleanup->events.size() == 1 && std::get<PointerEvent>(cleanup->events.front()).kind == PointerKind::cancel);
            f.rt.set_input_actor(cleanup->actor, cleanup->session);
            (void)f.rt.frame({seconds(0.04), std::move(cleanup->events)});
            f.rt.set_input_actor(std::nullopt);
        }
        expect(!p.take_input() && exit_of(reply) == 1);
        expect(!f.rt.state(f.rt.find("main/send")).pressed && f.sent == 0);
    };

    "a queued batch keeps its original issuer when the Runtime binding changes"_test = [] {
        Fixture f;
        agent::Protocol p(f.rt, {.input = true});
        std::vector<Value> reply;
        p.request(R"({"op":"input","events":[{"type":"click","node":"main/send"}]})",
                  [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        auto replacement = std::make_shared<AgentAccess>();
        replacement->set_policy({.mode = AgentMode::development});
        f.rt.set_agent_access(replacement);
        expect(!p.take_input() && exit_of(reply) == 1 && f.sent == 0);
    };

    "development controls use policy gates and log their authenticated actor"_test = [] {
        Fixture f;
        agent::Protocol p(f.rt);
        expect(set_log_filter("agent=info,*=warning").has_value());
        expect(exit_of(talk(p, R"({"op":"clock","action":"pause","actor":"agent:inspector"})")) == 0);
        expect(f.rt.time_state().paused);
        bool logged = false;
        for (const auto& record : recent_logs(100))
            if (record.category == "agent" &&
                record.message.find("Agent control clock by agent:inspector;via=service:development-session-") != std::string::npos)
                logged = true;
        expect(logged) << "the accepted control records its server-attributed actor";
        f.access->set_cap(openas::Cap{openas::Effect::read});
        expect(exit_of(talk(p, R"({"op":"clock","action":"resume"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"debug","bounds":true})")) == 1);
        expect(exit_of(talk(p, R"({"op":"profile","action":"start"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"pipeline","action":"remove","stage":"paint"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"clock","action":"show"})")) == 0 && f.rt.time_state().paused);
        expect(!f.rt.debug().bounds);
        expect(set_log_filter("*=info").has_value());
    };
}};
