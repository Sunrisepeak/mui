import std;
import openmui.core;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"core.agent_policy"};
    const Actor actor{"agent", "tester", {}};

    s.test("profile presets are typed separately from effect caps", [] {
        check(agent_mode_for_profile("dev") == AgentMode::development);
        for (std::string_view profile : {"release", "sanitize", "custom", "", "Dev"})
            check(agent_mode_for_profile(profile) == AgentMode::dual_user);
        check(parse_agent_mode("development") == AgentMode::development);
        check(parse_agent_mode("dual-user") == AgentMode::dual_user);
        check(!parse_cap("development") && !parse_cap("dual-user"));
    });

    s.test("only a host-created session receives the development owner grant", [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::development, .source = "test-dev-provider"});
        auto session = access.open_session();
        check(access.effective(session.get()).development_grant);
        check(access.decide("mail.send", Effect::external, false, actor) == openas::Decision::ask);
        check(access.decide("mail.send", Effect::external, false, actor, session.get()) == openas::Decision::run);
        const auto delegated = session->attribute({"person", "owner", {"person:forged"}});
        check(delegated.kind == "agent");
        check(delegated.via.size() == 1 && delegated.via.front() == session->owner());
        check(access.decide("mail.send", Effect::external, false, delegated) == openas::Decision::ask,
              "a forged or replayed delegation string is not a session handle");
        check(access.decide("permission.confirm", Effect::read, true, delegated, session.get()) == openas::Decision::refuse,
              "development does not manufacture the person's physical presence "
              "(S-10)");
        AgentAccess another;
        another.set_policy({.mode = AgentMode::development});
        check(another.decide("mail.send", Effect::external, false, actor, session.get()) == openas::Decision::refuse);
        check(!another.revoke_session(*session));
    });

    s.test("developer user and session restrictions intersect without widening", [&] {
        AgentAccess access;
        AgentPolicy policy{.mode = AgentMode::development};
        policy.developer.cap = openas::Cap{openas::Effect::irreversible};
        policy.developer.operations = std::vector<std::string>{"invoke", "input", "read.*"};
        policy.user.cap = openas::Cap{openas::Effect::reversible};
        policy.user.input = false;
        policy.user.operations = std::vector<std::string>{"invoke", "input"};
        access.set_policy(policy);
        AgentRestrictions limits;
        limits.cap = openas::Cap{openas::Effect::external};
        limits.input = true;
        limits.operations = std::vector<std::string>{"invoke", "input", "snapshot"};
        auto session = access.open_session(limits);
        auto state = access.effective(session.get());
        check(cap_name(state.cap) == "reversible");
        check(state.allows("invoke") && !state.allows("input") && !state.allows("snapshot"));
        check(access.decide("task.change", Effect::reversible, false, actor, session.get(), "invoke") == openas::Decision::run);
        check(access.decide("file.delete", Effect::irreversible, false, actor, session.get(), "invoke") == openas::Decision::refuse);
        check(access.decide("task.read", Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
        check(state.to_value().get("layers")->get("user")->get("input")->as_bool() == false);
        check(state.to_value().get("source")->as_string() == "explicit");
    });

    s.test("dual-user mode and disabled layers cannot be widened by a session", [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::dual_user});
        AgentRestrictions request;
        request.mode = AgentMode::development;
        request.enabled = true;
        auto session = access.open_session(request);
        check(!access.effective(session.get()).development_grant);
        check(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::ask);
        AgentPolicy policy{.mode = AgentMode::development};
        policy.developer.enabled = false;
        access.set_policy(policy);
        auto disabled = access.open_session(request);
        check(!access.effective(disabled.get()).enabled);
        check(access.decide("data.read", Effect::read, false, actor, disabled.get()) == openas::Decision::refuse);
        policy.developer.enabled = true;
        policy.user.mode = AgentMode::dual_user;
        access.set_policy(policy);
        check(access.effective(session.get()).mode == AgentMode::dual_user);
    });

    s.test("revocation does not silently create a replacement development grant", [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::development});
        auto session = access.open_session();
        check(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::run);
        access.revoke_all();
        check(!access.effective(session.get()).development_grant);
        check(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::ask);
        check(access.decide("data.read", Effect::read, false, actor, session.get()) == openas::Decision::run);
        check(access.revoke_session(*session));
        check(!access.revoke_session(*session));
        check(access.decide("data.read", Effect::read, false, actor, session.get()) == openas::Decision::refuse);
        auto fresh = access.open_session();
        check(access.decide("file.write", Effect::external, false, actor, fresh.get()) == openas::Decision::run,
              "only another trusted C++ session creation issues a fresh grant");
    });

    s.test("an explicit empty operation list denies all operations", [&] {
        AgentAccess access;
        AgentRestrictions limits;
        limits.operations = std::vector<std::string>{};
        auto session = access.open_session(limits);
        check(!access.effective(session.get()).allows("snapshot"));
        check(access.decide("data.read", Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    });
    return s.run(argc, argv);
}
