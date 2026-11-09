import std;
import omui.core;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

UTest cases{"core.agent_policy", [] {
    const Actor actor{"agent", "tester", {}};

    "profile presets are typed separately from effect caps"_test = [] {
        expect(agent_mode_for_profile("dev") == AgentMode::development);
        for (std::string_view profile : {"release", "sanitize", "custom", "", "Dev"})
            expect(agent_mode_for_profile(profile) == AgentMode::dual_user);
        expect(parse_agent_mode("development") == AgentMode::development);
        expect(parse_agent_mode("dual-user") == AgentMode::dual_user);
        expect(!parse_cap("development") && !parse_cap("dual-user"));
    };

    "only a host-created session receives the development owner grant"_test = [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::development, .source = "test-dev-provider"});
        auto session = access.open_session();
        expect(access.effective(session.get()).development_grant);
        expect(access.decide("mail.send", Effect::external, false, actor) == openas::Decision::ask);
        expect(access.decide("mail.send", Effect::external, false, actor, session.get()) == openas::Decision::run);
        const auto delegated = session->attribute({"person", "owner", {"person:forged"}});
        expect(delegated.kind == "agent");
        expect(delegated.via.size() == 1 && delegated.via.front() == session->owner());
        expect(access.decide("mail.send", Effect::external, false, delegated) == openas::Decision::ask) << "a forged or replayed delegation string is not a session handle";
        expect(access.decide("permission.confirm", Effect::read, true, delegated, session.get()) == openas::Decision::refuse) << "development does not manufacture the person's physical presence "
              "(S-10)";
        AgentAccess another;
        another.set_policy({.mode = AgentMode::development});
        expect(another.decide("mail.send", Effect::external, false, actor, session.get()) == openas::Decision::refuse);
        expect(!another.revoke_session(*session));
    };

    "developer user and session restrictions intersect without widening"_test = [&] {
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
        expect(cap_name(state.cap) == "reversible");
        expect(state.allows("invoke") && !state.allows("input") && !state.allows("snapshot"));
        expect(access.decide("task.change", Effect::reversible, false, actor, session.get(), "invoke") == openas::Decision::run);
        expect(access.decide("file.delete", Effect::irreversible, false, actor, session.get(), "invoke") == openas::Decision::refuse);
        expect(access.decide("task.read", Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
        expect(state.to_value().get("layers")->get("user")->get("input")->as_bool() == false);
        expect(state.to_value().get("source")->as_string() == "explicit");
    };

    "dual-user mode and disabled layers cannot be widened by a session"_test = [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::dual_user});
        AgentRestrictions request;
        request.mode = AgentMode::development;
        request.enabled = true;
        auto session = access.open_session(request);
        expect(!access.effective(session.get()).development_grant);
        expect(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::ask);
        AgentPolicy policy{.mode = AgentMode::development};
        policy.developer.enabled = false;
        access.set_policy(policy);
        auto disabled = access.open_session(request);
        expect(!access.effective(disabled.get()).enabled);
        expect(access.decide("data.read", Effect::read, false, actor, disabled.get()) == openas::Decision::refuse);
        policy.developer.enabled = true;
        policy.user.mode = AgentMode::dual_user;
        access.set_policy(policy);
        expect(access.effective(session.get()).mode == AgentMode::dual_user);
    };

    "revocation does not silently create a replacement development grant"_test = [&] {
        AgentAccess access;
        access.set_policy({.mode = AgentMode::development});
        auto session = access.open_session();
        expect(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::run);
        access.revoke_all();
        expect(!access.effective(session.get()).development_grant);
        expect(access.decide("file.write", Effect::external, false, actor, session.get()) == openas::Decision::ask);
        expect(access.decide("data.read", Effect::read, false, actor, session.get()) == openas::Decision::run);
        expect(access.revoke_session(*session));
        expect(!access.revoke_session(*session));
        expect(access.decide("data.read", Effect::read, false, actor, session.get()) == openas::Decision::refuse);
        auto fresh = access.open_session();
        expect(access.decide("file.write", Effect::external, false, actor, fresh.get()) == openas::Decision::run) << "only another trusted C++ session creation issues a fresh grant";
    };

    "an explicit empty operation list denies all operations"_test = [&] {
        AgentAccess access;
        AgentRestrictions limits;
        limits.operations = std::vector<std::string>{};
        auto session = access.open_session(limits);
        expect(!access.effective(session.get()).allows("snapshot"));
        expect(access.decide("data.read", Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    };
}};
