// Logs: levels per category, the fast check, the history tools read, and
// the JSON-lines form.
import std;
import omui.core;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
using mlibs::log::LogRecord;
using mlibs::log::log_enabled;
using mlibs::log::recent_logs;
using mlibs::log::set_log_filter;
using mlibs::log::set_log_history;
using mlibs::log::set_log_level;
using mlibs::log::set_log_sink;
using mlibs::log::Level;
using mlibs::log::info;
using mlibs::log::log_level;
using mlibs::log::warn;
}

using namespace mcpplibs::mtest;
using namespace omui;

namespace {

struct Captured {
    std::vector<std::string> lines;
};

void capture(void* user, const LogRecord& r) {
    static_cast<Captured*>(user)->lines.push_back(std::format("{}|{}|{}", name(r.level), r.category, r.message));
}

}  // namespace

UTest cases{"core.diag", [] {

    "levels per category; errors always pass"_test = [] {
        Captured got;
        set_log_sink(&capture, &got);
        expect(set_log_filter("layout=debug,text=error,*=warning").has_value());
        expect(log_level() == Level::debug) << "the fast check lets the most verbose category's level through";
        constexpr Log layout{"layout"}, text{"text"};
        layout.debug("{} nodes", 3);
        layout.verbose("not this");
        text.warn("not this either");
        text.error("an error passes {}", "always");
        info("not at warning");
        warn("the rest at {}", "warning");
        expect(got.lines == std::vector<std::string>{"debug|layout|3 nodes", "error|text|an error passes always", "warning|omui|the rest at warning"}) << std::format("{}", got.lines);
        expect(log_enabled(Level::debug, "layout") && !log_enabled(Level::info, "text") && !log_enabled(Level::info, "agent"));
        expect(!set_log_filter("layout=loud").has_value());
        set_log_level(Level::info);
        set_log_sink(nullptr, nullptr);
    };

    "the history keeps the last records for tools, as JSON lines"_test = [] {
        set_log_history(4);
        set_log_level(Level::trace);
        set_log_sink([](void*, const LogRecord&) {}, nullptr);
        constexpr Log agent{"agent"};
        for (int i = 0; i < 6; ++i) agent.info("request {}", i);
        agent.warn("a \"quoted\" warning\nwith a newline");
        auto recent = recent_logs(10);
        expect(recent.size() == 4 && recent.back().level == Level::warning && recent.front().message == "request 3");
        expect(recent_logs(10, Level::warning).size() == 1);
        const std::string json = to_json(recent.back());
        expect(json.starts_with(R"({"level":"warning","category":"agent","message":"a \"quoted\" warning\nwith a newline","file":)")) << json;
        auto parsed = parse_json(json);
        expect(parsed && parsed->get("line")->as_int() > 0 && parsed->get("time")->as_int() > 0);
        set_log_history(512);
        set_log_level(Level::info);
        set_log_sink(nullptr, nullptr);
    };
}};
