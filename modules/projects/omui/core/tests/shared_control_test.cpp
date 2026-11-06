import std;
import openmui.core;
import mlibs.control;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

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
    suite.test("native invocation conversion retains the bound issuer session and preview", [] {
        auto native=std::make_shared<mlibs::control::Access>(mlibs::control::policy_for_profile("dev"));
        auto ui=std::make_shared<AgentAccess>(native);auto session=ui->open_session();
        Invocation call{.args=Value{}.set("ignored_domain_payload",1),.answer="yes",
            .actor={"agent","reviewer",{session->owner()}},.remote=true,.dry_run=true,.access=ui,.session=session,.operation="invoke"};
        const auto converted=to_control_invocation(call);check(converted.has_value());if(!converted)return;
        check(converted->access==native && converted->session==session->control_session());
        check(converted->actor==to_openas(call.actor) && converted->remote && converted->dry_run &&
              converted->answer=="yes" && converted->operation=="invoke");
        mlibs::control::InvocationContext context;
        {
            mlibs::control::OperationScope scoped(context,native,*converted,{"animation.seek",openas::Effect::reversible});
            check(scoped.decision()==openas::Decision::run && !scoped.execute());
        }
        check(ui->revoke_session(*session));
        auto revoked=to_control_invocation(call);check(revoked.has_value());if(!revoked)return;
        mlibs::control::OperationScope scoped(context,native,*revoked,{"animation.seek",openas::Effect::reversible});
        check(scoped.decision()==openas::Decision::refuse && !scoped.execute(),"conversion does not renew a revoked session");
    });
    suite.test("unbound remote conversion refuses while local calls retain ordinary host semantics", [] {
        Invocation remote;remote.remote=true;
        const auto unbound=to_control_invocation(remote);check(!unbound && unbound.error()==ControlInvocationError::unbound_access);
        remote.access=std::make_shared<AgentAccess>();
        const auto missing=to_control_invocation(remote);check(!missing && missing.error()==ControlInvocationError::missing_session);
        Invocation local;local.actor={"custom-kind","host",{"program:caller"}};
        const auto converted=to_control_invocation(local);
        check(converted && !converted->remote && !converted->access && !converted->session &&
            converted->actor.kind==openas::Kind::program && converted->actor.via==local.actor.via);
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
