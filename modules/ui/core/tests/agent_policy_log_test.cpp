import std;
import openmui.core;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"core.agent_policy_log"};
    s.test("policy event sinks can inspect effective state without a held policy mutex", [] {
        struct Observer {
            AgentAccess access;
            int events = 0;
        } observer;
        check(set_log_filter("agent=debug,*=warning").has_value());
        set_log_sink(
            [](void* user, const LogRecord& record) {
                if (record.category != "agent") return;
                auto& observer = *static_cast<Observer*>(user);
                (void)observer.access.effective();
                ++observer.events;
            },
            &observer);
        observer.access.set_policy({.mode = AgentMode::development});
        auto session = observer.access.open_session();
        observer.access.set_cap(openas::Cap{openas::Effect::read});
        observer.access.revoke_all();
        check(observer.access.revoke_session(*session));
        set_log_sink(nullptr, nullptr);
        check(observer.events >= 5);
        check(set_log_filter("*=info").has_value());
    });
    return s.run(argc, argv);
}
