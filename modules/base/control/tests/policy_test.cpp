import std;
import mlibs.control;
import mcpplibs.mtest;

using namespace mlibs::control;
using namespace mcpplibs::mtest;

UTest cases{"control.policy", [] {
    const openas::Actor actor{openas::Kind::agent, "tester", {}};

    "profile presets are typed separately from effect caps"_test = [] {
        expect(mode_for_profile("dev") == Mode::development);
        for (std::string_view profile : {"release", "sanitize", "custom", "", "Dev"})
            expect(mode_for_profile(profile) == Mode::dual_user);
        expect(parse_mode("development") == Mode::development);
        expect(parse_mode("dual-user") == Mode::dual_user);
        expect(!parse_cap("development") && !parse_cap("dual-user"));
    };

    "only a host-created session receives the development owner grant"_test = [&] {
        Access access;
        access.set_policy({.mode = Mode::development, .source = "test-dev-provider"});
        auto session = access.open_session();
        expect(access.effective(session.get()).development_grant);
        expect(access.decide("mail.send", openas::Effect::external, false, actor) == openas::Decision::ask);
        expect(access.decide("mail.send", openas::Effect::external, false, actor, session.get()) == openas::Decision::run);
        const auto delegated = session->attribute({openas::Kind::person, "owner", {"person:forged"}});
        expect(delegated.kind == openas::Kind::agent);
        expect(delegated.via.size() == 1 && delegated.via.front() == session->owner());
        expect(access.decide("mail.send", openas::Effect::external, false, delegated) == openas::Decision::ask) << "a forged or replayed delegation string is not a session handle";
        expect(access.decide("permission.confirm", openas::Effect::read, true, delegated, session.get()) == openas::Decision::refuse) << "development does not manufacture the person's physical presence "
              "(S-10)";
        Access another;
        another.set_policy({.mode = Mode::development});
        expect(another.decide("mail.send", openas::Effect::external, false, actor, session.get()) == openas::Decision::refuse);
        expect(!another.revoke_session(*session));
    };

    "developer user and session restrictions intersect without widening"_test = [&] {
        Access access;
        Policy policy{.mode = Mode::development};
        policy.developer.cap = openas::Cap{openas::Effect::irreversible};
        policy.developer.operations = std::vector<std::string>{"invoke", "input", "read.*"};
        policy.user.cap = openas::Cap{openas::Effect::reversible};
        policy.user.input = false;
        policy.user.operations = std::vector<std::string>{"invoke", "input"};
        access.set_policy(policy);
        Restrictions limits;
        limits.cap = openas::Cap{openas::Effect::external};
        limits.input = true;
        limits.operations = std::vector<std::string>{"invoke", "input", "snapshot"};
        auto session = access.open_session(limits);
        auto state = access.effective(session.get());
        expect(cap_name(state.cap) == "reversible");
        expect(state.allows("invoke") && !state.allows("input") && !state.allows("snapshot"));
        expect(access.decide("task.change", openas::Effect::reversible, false, actor, session.get(), "invoke") == openas::Decision::run);
        expect(access.decide("file.delete", openas::Effect::irreversible, false, actor, session.get(), "invoke") == openas::Decision::refuse);
        expect(access.decide("task.read", openas::Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    };

    "dual-user mode and disabled layers cannot be widened by a session"_test = [&] {
        Access access;
        access.set_policy({.mode = Mode::dual_user});
        Restrictions request;
        request.mode = Mode::development;
        request.enabled = true;
        auto session = access.open_session(request);
        expect(!access.effective(session.get()).development_grant);
        expect(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::ask);
        Policy policy{.mode = Mode::development};
        policy.developer.enabled = false;
        access.set_policy(policy);
        auto disabled = access.open_session(request);
        expect(!access.effective(disabled.get()).enabled);
        expect(access.decide("data.read", openas::Effect::read, false, actor, disabled.get()) == openas::Decision::refuse);
        policy.developer.enabled = true;
        policy.user.mode = Mode::dual_user;
        access.set_policy(policy);
        expect(access.effective(session.get()).mode == Mode::dual_user);
    };

    "revocation does not silently create a replacement development grant"_test = [&] {
        Access access;
        access.set_policy({.mode = Mode::development});
        auto session = access.open_session();
        expect(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::run);
        access.revoke_all();
        expect(!access.effective(session.get()).development_grant);
        expect(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::ask);
        expect(access.decide("data.read", openas::Effect::read, false, actor, session.get()) == openas::Decision::run);
        expect(access.revoke_session(*session));
        expect(!access.revoke_session(*session));
        expect(access.decide("data.read", openas::Effect::read, false, actor, session.get()) == openas::Decision::refuse);
        auto fresh = access.open_session();
        expect(access.decide("file.write", openas::Effect::external, false, actor, fresh.get()) == openas::Decision::run) << "only another trusted C++ session creation issues a fresh grant";
    };

    "an explicit empty operation list denies all operations"_test = [&] {
        Access access;
        Restrictions limits;
        limits.operations = std::vector<std::string>{};
        auto session = access.open_session(limits);
        expect(!access.effective(session.get()).allows("snapshot"));
        expect(access.decide("data.read", openas::Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    };
    "omitting the operation uses the verb name instead of bypassing the filter"_test = [&] {
        Access access(policy_for_profile("dev"));
        auto session = access.open_session({.operations = std::vector<std::string>{"animation.capture"}});
        expect(access.decide("animation.capture", openas::Effect::read, false, actor, session.get()) == openas::Decision::run);
        expect(access.decide("animation.seek", openas::Effect::reversible, false, actor, session.get()) == openas::Decision::refuse);
    };
}};
