import std;
import omui.agent;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
std::vector<Value> talk(agent::Protocol& protocol, std::string_view request) {
    std::vector<Value> reply;
    protocol.request(request, [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
    return reply;
}
int exit_of(const std::vector<Value>& reply) {
    return reply.empty() || !reply.back().get("exitCode") ? -100 : static_cast<int>(reply.back().get("exitCode")->as_int());
}
struct Fixture {
    std::shared_ptr<AgentAccess> access = std::make_shared<AgentAccess>();
    Runtime rt{RuntimeOptions{.size = {240, 160}, .stages = Stages::layout, .access = access}};
    int sent = 0, confirmed = 0;
    explicit Fixture(AgentMode mode) {
        access->set_policy({.mode = mode, .source = "explicit-test-provider"});
        expect(rt.verbs().add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
            [this](const Value&) -> VerbResult { ++sent; return Value{}; }).has_value());
        expect(rt.verbs().add({.name = "person.confirm", .effect = Effect::read, .person = true},
            [this](const Value&) -> VerbResult { ++confirmed; return Value{}; }).has_value());
        rt.mount("main", column({}, {
            box({.id = "send", .width = 100, .height = 30, .label = "Send"}).prop("verb", Source{Value("mail.send"), {}, {}}),
        }));
        (void)rt.frame({seconds(0), {}});
    }
};
}

static UTest cases{"agent.policy", [] {
    "the host's development session grants real act and invoke but retains S-10"_test = [] {
        Fixture f(AgentMode::development);
        agent::Protocol p(f.rt);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 0);
        expect(exit_of(talk(p, R"({"op":"act","node":"main/send","action":"activate"})")) == 0);
        expect(f.sent == 2);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"person.confirm","actor":"person:owner","dev":true})")) == 1);
        expect(f.confirmed == 0);
        expect(f.rt.invoke("person.confirm").exit_code() == 0 && f.confirmed == 1) << "the trusted local fixture still has physical presence";
        const auto description = talk(p, R"({"op":"describe"})");
        expect(exit_of(description) == 0);
        const auto* policy = description.front().get("data")->get("agent_policy");
        expect(policy && policy->get("mode")->as_string() == "development") << description.front().json();
        if (policy) expect(policy->get("development_grant")->as_bool());
    };

    "remote mode and actor claims cannot enable development"_test = [] {
        Fixture f(AgentMode::dual_user);
        agent::Protocol p(f.rt);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send","mode":"development","dev":true,"actor":"person:owner"})")) == 2);
        expect(f.sent == 0);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send","answer":"send"})")) == 0);
        expect(f.sent == 1);
    };

    "a protocol does not replace its revoked development grant"_test = [] {
        Fixture f(AgentMode::development);
        agent::Protocol p(f.rt);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 0);
        f.access->revoke_all();
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 2);
        expect(exit_of(talk(p, R"({"op":"snapshot"})")) == 0);
        expect(f.sent == 1);
        const auto description = talk(p, R"({"op":"describe"})");
        const auto* policy = description.front().get("data")->get("agent_policy");
        expect(policy && !policy->get("development_grant")->as_bool()) << description.front().json();
    };

    "an endpoint made before startup binds the installed instance lazily"_test = [] {
        Runtime rt({.stages = Stages::semantics});
        agent::Protocol p(rt);
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development});
        rt.set_agent_access(access);
        int sent = 0;
        expect(rt.verbs().add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
            [&](const Value&) -> VerbResult { ++sent; return Value{}; }).has_value());
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 0 && sent == 1);
    };

    "protocol operations enforce narrowing and explain disabled policy"_test = [] {
        Fixture f(AgentMode::development);
        auto policy = f.access->policy();
        policy.user.operations = std::vector<std::string>{"snapshot"};
        policy.user.input = false;
        f.access->set_policy(policy);
        agent::Protocol p(f.rt, {.input = true});
        expect(exit_of(talk(p, R"({"op":"snapshot"})")) == 0);
        expect(exit_of(talk(p, R"({"op":"find","q":{"role":"button"}})")) == 1);
        expect(exit_of(talk(p, R"({"op":"invoke","verb":"mail.send"})")) == 1);
        expect(exit_of(talk(p, R"({"op":"input","events":[{"type":"click","node":"main/send"}]})")) == 1);
        policy.enabled = false;
        f.access->set_policy(policy);
        expect(exit_of(talk(p, R"({"op":"snapshot"})")) == 1);
        const auto description = talk(p, R"({"op":"describe"})");
        expect(exit_of(description) == 0);
        const auto* effective = description.front().get("data")->get("agent_policy");
        expect(effective && !effective->get("enabled")->as_bool()) << description.front().json();
        if (effective) expect(effective->get("layers")->get("user")->get("operations")->list()->size() == 1);
        expect(f.sent == 0);
    };

    "input queued before a cap reduction is refused before delivery"_test = [] {
        Fixture f(AgentMode::development);
        agent::Protocol p(f.rt, {.input = true});
        std::vector<Value> reply;
        p.request(R"({"op":"input","events":[{"type":"click","node":"main/send"}]})",
            [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        expect(reply.empty()) << "input is deferred until frames run";
        f.access->set_cap(openas::Cap{openas::Effect::read});
        expect(!p.take_input()) << "the queued batch must not escape its new cap";
        expect(exit_of(reply) == 1) << (reply.empty() ? "missing refusal" : reply.front().json());
        expect(f.sent == 0);
    };
    "a person's input is theirs (W12): outside development, timeline and explain withhold it until the person says yes"_test = [] {
        Fixture f(AgentMode::dual_user);
        f.rt.set_record_level(RecordLevel::full);
        const Point at = f.rt.screen_bounds(f.rt.find("main/send")).center();
        (void)f.rt.frame({seconds(0.1), {PointerEvent{.position = at},
                                        PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1},
                                        PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}}});
        agent::Protocol p(f.rt);
        const auto withheld = talk(p, R"({"op":"timeline"})");
        expect(exit_of(withheld) == 2) << (withheld.empty() ? "no answer" : withheld.back().json());
        expect(withheld.front().get("data")->get("withheld")->as_int() > 0) << withheld.front().json();
        const auto press = talk(p, std::format(R"({{"op":"explain","at":[{},{}]}})", at.x, at.y));
        expect(exit_of(press) == 2) << (press.empty() ? "no answer" : press.back().json());
        // The person said yes: their records, now and for the rest of the session.
        const auto shown = talk(p, R"({"op":"timeline","answer":"yes"})");
        expect(exit_of(shown) == 0 && !shown.front().get("data")->get("records")->list()->empty()) << (shown.empty() ? "no answer" : shown.back().json());
        expect(exit_of(talk(p, std::format(R"({{"op":"explain","at":[{},{}]}})", at.x, at.y))) == 0) << "explained once allowed";
        // In development an agent reads them without asking.
        Fixture development(AgentMode::development);
        development.rt.set_record_level(RecordLevel::full);
        (void)development.rt.frame({seconds(0.1), {PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1},
                                                  PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}}});
        agent::Protocol q(development.rt);
        expect(exit_of(talk(q, R"({"op":"timeline"})")) == 0) << "development: all of it";
    };
}};
