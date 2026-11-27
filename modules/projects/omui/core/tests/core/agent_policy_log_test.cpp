import std;
import omui.core;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::LogRecord;
using mlibs::log::set_log_filter;
using mlibs::log::set_log_sink;
}

using namespace omui;
using namespace mcpplibs::mtest;

static UTest cases{"core.agent_policy_log", [] {
    "policy event sinks can inspect effective state without a held policy mutex"_test = [] {
        struct Observer {
            AgentAccess access;
            int events = 0;
        } observer;
        expect(set_log_filter("agent=debug,*=warning").has_value());
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
        expect(observer.access.revoke_session(*session));
        set_log_sink(nullptr, nullptr);
        expect(observer.events >= 5);
        expect(set_log_filter("*=info").has_value());
    };
}};
