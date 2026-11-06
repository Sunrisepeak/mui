import std;
import openmui.host.web;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"host.web.agent_policy"};
    s.test("the Web input path passes its trusted development session to the Runtime", [] {
        Web web({.size = {240, 160}});
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development});
        web.runtime().set_agent_access(access);
        int sent = 0;
        check(web.runtime()
                  .verbs()
                  .add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
                       [&](const Value&) -> VerbResult {
                           ++sent;
                           return Value{};
                       })
                  .has_value());
        web.mount(
            "main",
            column({},
                   {
                       box({.id = "send", .width = 100, .height = 30, .label = "Send"}).prop("verb", Source{Value("mail.send"), {}, {}}),
                   }));
        (void)web.step();
        std::vector<Value> reply;
        web.request(R"({"op":"input","actor":"agent:browser","events":[{"type":"click","node":"main/send"}]})",
                    [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        for (int i = 0; i < 8 && reply.empty(); ++i)
            (void)web.step();
        check(!reply.empty() && reply.back().get("exitCode")->as_int() == 0);
        check(sent == 1);
        const auto journal = web.runtime().verbs().journal();
        check(journal.size() == 1);
        if (!journal.empty()) check(journal.front().actor.starts_with("agent:browser;via=service:development-session-"));
    });
    s.test("the Web host input switch is narrower than its Runtime preset", [] {
        Web web({.size = {240, 160}, .agent_input = false});
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development});
        web.runtime().set_agent_access(access);
        std::vector<Value> reply;
        web.request(R"({"op":"input","events":[{"type":"text","text":"hello"}]})",
                    [&](std::string_view line) { reply.push_back(parse_json(line).value()); });
        (void)web.step();
        check(!reply.empty() && reply.back().get("exitCode")->as_int() == 1);
    });
    return s.run(argc, argv);
}
