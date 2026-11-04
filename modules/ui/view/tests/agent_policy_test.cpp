import std;
import openmui.core;
import openmui.view;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {
std::shared_ptr<AgentAccess> access_with(AgentMode mode, openas::Effect cap = openas::Effect::external) {
    auto access = std::make_shared<AgentAccess>();
    access->set_policy({.mode = mode, .cap = openas::Cap{cap}, .source = "embedded-test-provider"});
    return access;
}
void add_send(Runtime& rt, int& ran) {
    check(rt.verbs()
              .add({.name = "outside.send", .effect = Effect::external, .answer = "send"},
                   [&](const Value&) -> VerbResult {
                       ++ran;
                       return Value{};
                   })
              .has_value());
}
} // namespace

int main(int argc, char** argv) {
    Suite s{"view.agent_policy"};
    s.test("bound runtimes isolate caps grants and sessions from the legacy "
           "fallback",
           [] {
               auto dev = access_with(AgentMode::development);
               auto read = access_with(AgentMode::dual_user, openas::Effect::read);
               Runtime a({.stages = Stages::semantics, .access = dev});
               Runtime b({.stages = Stages::semantics, .access = read});
               int first = 0, second = 0;
               add_send(a, first);
               add_send(b, second);
               auto session = dev->open_session();
               Invocation call{.actor = {"agent", "tester", {}}, .remote = true, .session = session, .operation = "invoke"};
               agent_access().set_cap(openas::Cap::none());
               agent_access().revoke_all();
               check(a.invoke("outside.send", call).exit_code() == 0);
               check(b.invoke("outside.send", call).exit_code() == 1);
               check(first == 1 && second == 0);
               check(a.verbs().journal().front().actor.find(session->owner()) != std::string::npos);
               check(&a.agent_access() == dev.get() && &b.agent_access() == read.get());
               agent_access().set_cap(openas::Cap{openas::Effect::external});
           });

    s.test("the runtime binding overrides a foreign Invocation access pointer", [] {
        auto dev = access_with(AgentMode::development);
        auto read = access_with(AgentMode::dual_user, openas::Effect::read);
        Runtime rt({.stages = Stages::semantics, .access = read});
        int ran = 0;
        add_send(rt, ran);
        auto session = dev->open_session();
        Invocation call{.actor = {"agent", "tester", {}}, .remote = true, .access = dev, .session = session, .operation = "invoke"};
        const auto reply = rt.invoke("outside.send", call);
        check(reply.exit_code() == 1 && ran == 0, reply.ndjson());
        check(reply.ndjson().find("session that is not valid") != std::string::npos);
    });

    s.test("raw Verbs and Runtime entries share their instance's nested authority", [] {
        auto access = access_with(AgentMode::development);
        AgentRestrictions limits;
        limits.cap = openas::Cap{openas::Effect::read};
        auto session = access->open_session(limits);
        Runtime rt({.stages = Stages::semantics, .access = access});
        int ran = 0;
        add_send(rt, ran);
        Envelope nested;
        check(rt.verbs()
                  .add({.name = "wrapper.read", .effect = Effect::read},
                       [&](const Value&) -> VerbResult {
                           nested = rt.invoke("outside.send", {.answer = "send"});
                           return Value{};
                       })
                  .has_value());
        Invocation call{.actor = {"agent", "tester", {}}, .remote = true, .session = session, .operation = "invoke"};
        check(rt.verbs().invoke("wrapper.read", call).exit_code() == 0);
        check(nested.exit_code() == 1 && ran == 0, nested.ndjson());
        check(rt.verbs().journal().front().actor.find(session->owner()) != std::string::npos);
    });

    s.test("input carries the same session restrictions into callbacks", [] {
        auto access = access_with(AgentMode::development);
        AgentRestrictions limits;
        limits.cap = openas::Cap{openas::Effect::read};
        auto session = access->open_session(limits);
        Runtime rt({.stages = Stages::semantics, .access = access});
        int ran = 0;
        add_send(rt, ran);
        rt.set_input_actor(Actor{"agent", "keyboard", {}}, session);
        const auto denied = rt.invoke("outside.send", {.answer = "send"});
        check(denied.exit_code() == 1 && ran == 0, denied.ndjson());
        rt.set_input_actor(std::nullopt);
        check(rt.invoke("outside.send", {.answer = "send"}).exit_code() == 0);
        check(ran == 1);
    });
    return s.run(argc, argv);
}
