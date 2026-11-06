import std;
import openmui.core;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"core.agent_context"};
    s.test("a remote verb callback cannot turn its nested call into local authority", [] {
        auto& access = agent_access();
        access.set_cap(openas::Cap{openas::Effect::read});
        access.revoke_all();
        Verbs verbs;
        int ran = 0;
        check(verbs.add({.name = "outside.send", .effect = Effect::external, .answer = "send"},
            [&](const Value&) -> VerbResult { ++ran; return Value{}; }).has_value());
        Envelope nested;
        check(verbs.add({.name = "wrapper.read", .effect = Effect::read}, [&](const Value&) -> VerbResult {
            nested = verbs.invoke("outside.send", {.answer = "send"});
            return Value{};
        }).has_value());
        auto result = verbs.invoke("wrapper.read", {.actor = {"agent", "tester", {}}, .remote = true});
        check(result.exit_code() == 0, result.ndjson());
        check(nested.exit_code() == 1, nested.ndjson());
        check(ran == 0, "a nested callback must keep the remote cap");
        check(verbs.journal().size() == 2);
        check(verbs.journal().front().actor == "agent:tester", "the nested call keeps its request's attribution");
        access.set_cap(openas::Cap{openas::Effect::external});
    });
    return s.run(argc, argv);
}
