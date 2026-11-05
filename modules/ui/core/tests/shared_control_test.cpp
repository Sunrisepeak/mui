import std;
import openmui.core;
import mlibs.control;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite suite{"core.shared_control"};
    suite.test("UI and independent consumers share the actual policy and session issuer", [] {
        auto access = std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev"));
        auto ui = std::make_shared<AgentAccess>(access);
        auto session = ui->open_session();
        check(ui->control_binding() == access && access->owns(*session->control_session()));
        Verbs verbs;
        verbs.set_agent_access(ui);
        int changes = 0;
        check(verbs.add({.name = "document.edit"}, [&](const Value&) -> VerbResult { ++changes; return Value{}; }).has_value());
        Invocation call{.actor = {"agent", "reviewer", {}}, .remote = true, .access = ui, .session = session};
        check(verbs.invoke("document.edit", call).exit_code() == 0 && changes == 1);
        auto policy = access->policy();
        policy.user.operations = std::vector<std::string>{"animation.*"};
        access->set_policy(policy);
        check(verbs.invoke("document.edit", call).exit_code() != 0 && changes == 1);
        check(access->decide("animation.seek", openas::Effect::reversible, false,
            {openas::Kind::agent, "reviewer", {}}, session->control_session().get(), "animation.seek") == openas::Decision::run);
        const auto effective = ui->effective(session.get());
        check(!effective.allows("invoke") && effective.allows("animation.seek"));
        check(effective.to_value().get("layers")->get("user")->get("operations")->list()->size() == 1);
        check(access->revoke_session(*session->control_session()));
        check(!ui->effective(session.get()).session_valid && !ui->revoke_session(*session));
    });
    suite.test("legacy UI value aggregates and actor attribution remain available", [] {
        AgentPolicyState state{.mode = AgentMode::dual_user, .source = "compatibility"};
        check(state.allows("snapshot") && state.to_value().get("source")->as_string() == "compatibility");
        AgentAccess ui;
        auto ordinary = ui.open_session();
        const Actor unusual{"custom-kind", "reviewer", {"service:reported"}};
        check(ordinary->attribute(unusual).kind == unusual.kind);
        ui.set_policy(agent_policy_for_profile("dev"));
        auto development = ui.open_session();
        check(development->attribute(unusual).kind == "agent");
        check(development->attribute(unusual).via == std::vector{development->owner()});
        check(ui.decide("external.write", Effect::external, false, unusual, development.get()) == openas::Decision::run);
        ui.revoke_all();
        check(ui.decide("external.write", Effect::external, false, unusual, development.get()) == openas::Decision::ask);
    });
    return suite.run(argc, argv);
}
