import std;
import omui.core;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

UTest cases{"core.agent_context", [] {
    "a remote verb callback cannot turn its nested call into local authority"_test = [] {
        auto& access = agent_access();
        access.set_cap(openas::Cap{openas::Effect::read});
        access.revoke_all();
        Verbs verbs;
        int ran = 0;
        expect(verbs.add({.name = "outside.send", .effect = Effect::external, .answer = "send"},
            [&](const Value&) -> VerbResult { ++ran; return Value{}; }).has_value());
        Envelope nested;
        expect(verbs.add({.name = "wrapper.read", .effect = Effect::read}, [&](const Value&) -> VerbResult {
            nested = verbs.invoke("outside.send", {.answer = "send"});
            return Value{};
        }).has_value());
        auto result = verbs.invoke("wrapper.read", {.actor = {"agent", "tester", {}}, .remote = true});
        expect(result.exit_code() == 0) << result.ndjson();
        expect(nested.exit_code() == 1) << nested.ndjson();
        expect(ran == 0) << "a nested callback must keep the remote cap";
        expect(verbs.journal().size() == 2);
        expect(verbs.journal().front().actor == "agent:tester") << "the nested call keeps its request's attribution";
        access.set_cap(openas::Cap{openas::Effect::external});
    };
}};
