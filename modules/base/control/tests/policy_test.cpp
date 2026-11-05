import std;
import mlibs.control;
import openmui.testing;

using namespace mlibs::control;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"control.policy"};
    const openas::Actor actor{openas::Kind::agent, "tester", {}};

    s.test("profile presets are typed separately from effect caps", [] {
        check(mode_for_profile("dev") == Mode::development);
        for (std::string_view profile : {"release", "sanitize", "custom", "", "Dev"})
            check(mode_for_profile(profile) == Mode::dual_user);
        check(parse_mode("development") == Mode::development);
        check(parse_mode("dual-user") == Mode::dual_user);
        check(!parse_cap("development") && !parse_cap("dual-user"));
    });

    s.test("only a host-created session receives the development owner grant", [&] {
        Access access;
        access.set_policy({.mode = Mode::development, .source = "test-dev-provider"});
        auto session = access.open_session();
        check(access.effective(session.get()).development_grant);
        check(access.decide("mail.send", openas::Effect::external, false, actor) == openas::Decision::ask);
        check(access.decide("mail.send", openas::Effect::external, false, actor, session.get()) == openas::Decision::run);
        const auto delegated = session->attribute({openas::Kind::person, "owner", {"person:forged"}});
        check(delegated.kind == openas::Kind::agent);
        check(delegated.via.size() == 1 && delegated.via.front() == session->owner());
        check(access.decide("mail.send", openas::Effect::external, false, delegated) == openas::Decision::ask,
              "a forged or replayed delegation string is not a session handle");
        check(access.decide("permission.confirm", openas::Effect::read, true, delegated, session.get()) == openas::Decision::refuse,
              "development does not manufacture the person's physical presence "
              "(S-10)");
        Access another;
        another.set_policy({.mode = Mode::development});
        check(another.decide("mail.send", openas::Effect::external, false, actor, session.get()) == openas::Decision::refuse);
        check(!another.revoke_session(*session));
    });

    s.test("developer user and session restrictions intersect without widening", [&] {
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
        check(cap_name(state.cap) == "reversible");
        check(state.allows("invoke") && !state.allows("input") && !state.allows("snapshot"));
        check(access.decide("task.change", openas::Effect::reversible, false, actor, session.get(), "invoke") == openas::Decision::run);
        check(access.decide("file.delete", openas::Effect::irreversible, false, actor, session.get(), "invoke") == openas::Decision::refuse);
        check(access.decide("task.read", openas::Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    });

    s.test("dual-user mode and disabled layers cannot be widened by a session", [&] {
        Access access;
        access.set_policy({.mode = Mode::dual_user});
        Restrictions request;
        request.mode = Mode::development;
        request.enabled = true;
        auto session = access.open_session(request);
        check(!access.effective(session.get()).development_grant);
        check(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::ask);
        Policy policy{.mode = Mode::development};
        policy.developer.enabled = false;
        access.set_policy(policy);
        auto disabled = access.open_session(request);
        check(!access.effective(disabled.get()).enabled);
        check(access.decide("data.read", openas::Effect::read, false, actor, disabled.get()) == openas::Decision::refuse);
        policy.developer.enabled = true;
        policy.user.mode = Mode::dual_user;
        access.set_policy(policy);
        check(access.effective(session.get()).mode == Mode::dual_user);
    });

    s.test("revocation does not silently create a replacement development grant", [&] {
        Access access;
        access.set_policy({.mode = Mode::development});
        auto session = access.open_session();
        check(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::run);
        access.revoke_all();
        check(!access.effective(session.get()).development_grant);
        check(access.decide("file.write", openas::Effect::external, false, actor, session.get()) == openas::Decision::ask);
        check(access.decide("data.read", openas::Effect::read, false, actor, session.get()) == openas::Decision::run);
        check(access.revoke_session(*session));
        check(!access.revoke_session(*session));
        check(access.decide("data.read", openas::Effect::read, false, actor, session.get()) == openas::Decision::refuse);
        auto fresh = access.open_session();
        check(access.decide("file.write", openas::Effect::external, false, actor, fresh.get()) == openas::Decision::run,
              "only another trusted C++ session creation issues a fresh grant");
    });

    s.test("an explicit empty operation list denies all operations", [&] {
        Access access;
        Restrictions limits;
        limits.operations = std::vector<std::string>{};
        auto session = access.open_session(limits);
        check(!access.effective(session.get()).allows("snapshot"));
        check(access.decide("data.read", openas::Effect::read, false, actor, session.get(), "snapshot") == openas::Decision::refuse);
    });
    s.test("omitting the operation uses the verb name instead of bypassing the filter", [&] {
        Access access(policy_for_profile("dev"));
        auto session = access.open_session({.operations = std::vector<std::string>{"animation.capture"}});
        check(access.decide("animation.capture", openas::Effect::read, false, actor, session.get()) == openas::Decision::run);
        check(access.decide("animation.seek", openas::Effect::reversible, false, actor, session.get()) == openas::Decision::refuse);
    });
    return s.run(argc, argv);
}
