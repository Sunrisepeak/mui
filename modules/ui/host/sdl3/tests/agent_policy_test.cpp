import std;
import openmui.host.sdl3;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"host.sdl3.agent_policy"};
    s.test("the SDL frame preserves the session of endpoint input", [] {
        const auto dir = scratch_dir("sdl-agent-policy");
        DesktopOptions options;
        options.app = "policy-desktop";
        options.size = {240, 160};
        options.agent_directory = dir;
        options.video_driver = "offscreen";
        options.render_driver = "software";
        options.vsync = false;
        options.accessibility = false;
        options.renderer = DesktopOptions::Renderer::cpu;
        Desktop host(options);
        const auto opened = host.open();
        check(opened.has_value(), opened ? "" : opened.error());
        if (!opened) return;
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
            if (auto client = agent::Client::connect("policy-desktop", dir))
                reply = client->call(
                    parse_json(R"({"op":"input","actor":"agent:desktop","events":[{"type":"click","node":"main/send"}]})").value(),
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
        if (!journal.empty()) check(journal.front().actor.starts_with("agent:desktop;via=service:development-session-"));
    });
    return s.run(argc, argv);
}
