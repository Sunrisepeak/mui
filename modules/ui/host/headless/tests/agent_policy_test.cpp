import std;
import openmui.host.headless;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

int main(int argc, char** argv) {
    if constexpr (!threads) {
        // This transport and its client require background threads. Web
        // authorization is exercised through the in-process/browser bridge;
        // do not report a local IPC policy case as having run there.
        std::println("host.headless.agent_policy: skipped (requires threaded local IPC)");
        return 0;
    }
    Suite s{"host.headless.agent_policy"};
    s.test("H3 forwards the trusted session of real endpoint input", [] {
        const auto dir = scratch_dir("headless-agent-policy");
        Headless host({.level = HeadlessLevel::session, .size = {240, 160}, .app = "policy-headless", .endpoint_directory = dir});
        check(host.endpoint() != nullptr, "this test requires the real local endpoint");
        if (!host.endpoint()) return;
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development});
        host.runtime().set_agent_access(access);
        int sent = 0;
        check(host.runtime()
                  .verbs()
                  .add({.name = "mail.send", .effect = Effect::external, .answer = "send"},
                       [&](const Value&) -> VerbResult {
                           ++sent;
                           return Value{};
                       })
                  .has_value());
        host.mount(
            "main",
            column({},
                   {
                       box({.id = "send", .width = 100, .height = 30, .label = "Send"}).prop("verb", Source{Value("mail.send"), {}, {}}),
                   }));
        (void)host.step();
        std::vector<Value> reply;
        std::atomic<bool> done{false};
        std::jthread client_thread([&] {
            if (auto client = agent::Client::connect("policy-headless", dir))
                reply = client->call(
                    parse_json(R"({"op":"input","actor":"agent:headless","events":[{"type":"click","node":"main/send"}]})").value(),
                    std::chrono::seconds(2));
            done = true;
        });
        for (int i = 0; i < 3000 && !done; ++i) {
            (void)host.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client_thread.join();
        check(!reply.empty() && reply.back().get("exitCode")->as_int() == 0);
        check(sent == 1);
        const auto journal = host.runtime().verbs().journal();
        check(journal.size() == 1);
        if (!journal.empty()) check(journal.front().actor.starts_with("agent:headless;via=service:development-session-"));
    });
    return s.run(argc, argv);
}
