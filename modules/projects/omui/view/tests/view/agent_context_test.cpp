import std;
import omui.core;
import omui.view;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

static UTest cases{"view.agent_context", [] {
    "nested Runtime callbacks retain remote authority and S-10"_test = [] {
        auto& access = agent_access();
        access.set_cap(openas::Cap{openas::Effect::read});
        access.revoke_all();
        Runtime rt({.stages = Stages::semantics});
        int external = 0, person = 0;
        expect(rt.verbs().add({.name = "outside.send", .effect = Effect::external, .answer = "send"},
            [&](const Value&) -> VerbResult { ++external; return Value{}; }).has_value());
        expect(rt.verbs().add({.name = "person.confirm", .effect = Effect::read, .person = true},
            [&](const Value&) -> VerbResult { ++person; return Value{}; }).has_value());
        Envelope send, confirm;
        expect(rt.verbs().add({.name = "wrapper.read", .effect = Effect::read}, [&](const Value&) -> VerbResult {
            send = rt.invoke("outside.send", {.answer = "send"});
            confirm = rt.invoke("person.confirm");
            return Value{};
        }).has_value());
        const auto outer = rt.invoke("wrapper.read", {.actor = {"agent", "tester", {}}, .remote = true});
        expect(outer.exit_code() == 0) << outer.ndjson();
        expect(send.exit_code() == 1) << send.ndjson();
        expect(confirm.exit_code() == 1) << confirm.ndjson();
        expect(external == 0 && person == 0) << "callbacks cannot manufacture local presence or escape the cap";
        expect(rt.verbs().journal().size() == 3);
        expect(rt.verbs().journal()[0].actor == "agent:tester");
        expect(rt.verbs().journal()[1].actor == "agent:tester");
        // A trusted local fixture outside a remote call still supplies presence.
        expect(rt.invoke("person.confirm").exit_code() == 0);
        expect(person == 1);
        access.set_cap(openas::Cap{openas::Effect::external});
    };
}};
