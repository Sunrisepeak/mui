// Logs: levels per category, the fast check, the history tools read, and
// the JSON-lines form.
import std;
import openmui.core;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct Captured {
    std::vector<std::string> lines;
};

void capture(void* user, const LogRecord& r) {
    static_cast<Captured*>(user)->lines.push_back(std::format("{}|{}|{}", name(r.level), r.category, r.message));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"core.diag"};

    s.test("levels per category; errors always pass", [] {
        Captured got;
        set_log_sink(&capture, &got);
        check(set_log_filter("layout=debug,text=error,*=warning").has_value());
        check(log_level() == Level::debug, "the fast check lets the most verbose category's level through");
        constexpr Log layout{"layout"}, text{"text"};
        layout.debug("{} nodes", 3);
        layout.verbose("not this");
        text.warn("not this either");
        text.error("an error passes {}", "always");
        info("not at warning");
        warn("the rest at {}", "warning");
        check(got.lines == std::vector<std::string>{"debug|layout|3 nodes", "error|text|an error passes always", "warning|omui|the rest at warning"},
              std::format("{}", got.lines));
        check(log_enabled(Level::debug, "layout") && !log_enabled(Level::info, "text") && !log_enabled(Level::info, "agent"));
        check(!set_log_filter("layout=loud").has_value());
        set_log_level(Level::info);
        set_log_sink(nullptr, nullptr);
    });

    s.test("the history keeps the last records for tools, as JSON lines", [] {
        set_log_history(4);
        set_log_level(Level::trace);
        set_log_sink([](void*, const LogRecord&) {}, nullptr);
        constexpr Log agent{"agent"};
        for (int i = 0; i < 6; ++i) agent.info("request {}", i);
        agent.warn("a \"quoted\" warning\nwith a newline");
        auto recent = recent_logs(10);
        check(recent.size() == 4 && recent.back().level == Level::warning && recent.front().message == "request 3");
        check(recent_logs(10, Level::warning).size() == 1);
        const std::string json = to_json(recent.back());
        check(json.starts_with(R"({"level":"warning","category":"agent","message":"a \"quoted\" warning\nwith a newline","file":)"), json);
        auto parsed = parse_json(json);
        check(parsed && parsed->get("line")->as_int() > 0 && parsed->get("time")->as_int() > 0);
        set_log_history(512);
        set_log_level(Level::info);
        set_log_sink(nullptr, nullptr);
    });

    return s.run(argc, argv);
}
