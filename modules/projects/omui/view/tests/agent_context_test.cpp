import std;
import openmui.core;
import openmui.view;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"view.agent_context"};
    s.test("nested Runtime callbacks retain remote authority and S-10", [] {
        auto& access = agent_access();
        access.set_cap(openas::Cap{openas::Effect::read});
        access.revoke_all();
        Runtime rt({.stages = Stages::semantics});
        int external = 0, person = 0;
        check(rt.verbs().add({.name = "outside.send", .effect = Effect::external, .answer = "send"},
            [&](const Value&) -> VerbResult { ++external; return Value{}; }).has_value());
        check(rt.verbs().add({.name = "person.confirm", .effect = Effect::read, .person = true},
            [&](const Value&) -> VerbResult { ++person; return Value{}; }).has_value());
        Envelope send, confirm;
        check(rt.verbs().add({.name = "wrapper.read", .effect = Effect::read}, [&](const Value&) -> VerbResult {
            send = rt.invoke("outside.send", {.answer = "send"});
            confirm = rt.invoke("person.confirm");
            return Value{};
        }).has_value());
        const auto outer = rt.invoke("wrapper.read", {.actor = {"agent", "tester", {}}, .remote = true});
        check(outer.exit_code() == 0, outer.ndjson());
        check(send.exit_code() == 1, send.ndjson());
        check(confirm.exit_code() == 1, confirm.ndjson());
        check(external == 0 && person == 0, "callbacks cannot manufacture local presence or escape the cap");
        check(rt.verbs().journal().size() == 3);
        check(rt.verbs().journal()[0].actor == "agent:tester");
        check(rt.verbs().journal()[1].actor == "agent:tester");
        // A trusted local fixture outside a remote call still supplies presence.
        check(rt.invoke("person.confirm").exit_code() == 0);
        check(person == 1);
        access.set_cap(openas::Cap{openas::Effect::external});
    });
    return s.run(argc, argv);
}
