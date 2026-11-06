// The public diagnostics API is independently consumable without UI/core.
import std;
import mlibs.log;
import mlibs.testing;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
using mlibs::log::LogRecord;
using mlibs::log::LoggedRecord;
using mlibs::log::log_enabled;
using mlibs::log::recent_logs;
using mlibs::log::set_log_filter;
using mlibs::log::set_log_history;
using mlibs::log::set_log_level;
using mlibs::log::set_log_sink;
using mlibs::log::Level;
using mlibs::log::info;
using mlibs::log::invariant;
using mlibs::log::log_level;
using mlibs::log::warn;
}

using namespace omui;
using namespace mlibs::testing;

namespace {
struct Entry { Level level; std::string category, message, file; std::uint32_t line; };
struct Capture { std::vector<Entry> entries; };
void capture(void* user, const LogRecord& record) {
    static_cast<Capture*>(user)->entries.push_back({record.level, std::string(record.category), std::string(record.message),
                                                  record.where.file_name(), record.where.line()});
}
void quiet(void*, const LogRecord&) {}
struct Restore {
    ~Restore() { set_log_sink(nullptr, nullptr); set_log_level(Level::info); set_log_history(512); }
};
}

int main(int argc, char** argv) {
    Suite suite{"log.public"};
    suite.test("independent category sink preserves formatted text and the caller location", [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_level(Level::trace); set_log_sink(capture, &got);
        constexpr Log objects{"hanim.object"};
        const auto call = std::source_location::current();
        objects.info("prepared {} objects", 12);
        check_eq(got.entries.size(), std::size_t{1});
        if (got.entries.empty()) return;
        check(got.entries[0].level == Level::info && got.entries[0].category == "hanim.object");
        check_eq(got.entries[0].message, std::string("prepared 12 objects"));
        check_eq(got.entries[0].file, std::string(call.file_name()));
        check_eq(got.entries[0].line, call.line() + 1);
    });
    suite.test("category filters retain the previous policy after an invalid update and errors always pass", [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_sink(capture, &got);
        check(set_log_filter(" objects=debug,render=error,*=warning ").has_value());
        check(log_level() == Level::debug);
        check(!set_log_filter("objects=loud").has_value());
        constexpr Log objects{"objects"}, render{"render"};
        objects.debug("accepted"); objects.verbose("filtered"); render.warn("filtered");
        render.error("always"); info("filtered"); warn("fallback");
        check_eq(got.entries.size(), std::size_t{3});
        if (got.entries.size() != 3) return;
        check_eq(got.entries[0].message, std::string("accepted"));
        check_eq(got.entries[1].message, std::string("always"));
        check_eq(got.entries[2].message, std::string("fallback"));
        check(set_log_filter("*=off").has_value());
        check(!log_enabled(Level::warning, "objects") && log_enabled(Level::error, "objects"));
    });
    suite.test("bounded history owns its data and filters copies in chronological order", [] {
        Restore restore;
        set_log_sink(quiet, nullptr); set_log_level(Level::trace); set_log_history(0); set_log_history(3);
        constexpr Log objects{"objects"};
        for (int i = 0; i < 5; ++i) objects.info("object {}", i);
        objects.warn("last warning");
        auto history = recent_logs(10);
        check_eq(history.size(), std::size_t{3});
        if (history.size() != 3) return;
        check_eq(history[0].message, std::string("object 3"));
        check_eq(history[1].message, std::string("object 4"));
        check_eq(history[2].message, std::string("last warning"));
        check(history[2].line > 0 && history[2].unix_ms > 0 && !history[2].file.empty());
        check_eq(recent_logs(10, Level::warning).size(), std::size_t{1});
        history.back().message = "changed caller copy";
        check_eq(recent_logs(1).back().message, std::string("last warning"));
        set_log_history(1); check_eq(recent_logs().size(), std::size_t{1});
        set_log_history(0); objects.error("not retained"); check(recent_logs().empty());
    });
    suite.test("sink reentry can inspect history change filters and emit an inner record", [] {
        Restore restore;
        struct State { bool entered = false, history_visible = false; std::vector<std::string> seen; } state;
        set_log_history(0); set_log_history(8); set_log_level(Level::trace);
        set_log_sink([](void* pointer, const LogRecord& record) {
            auto& state = *static_cast<State*>(pointer);
            state.seen.emplace_back(record.message);
            if (state.entered) return;
            state.entered = true;
            const auto history = recent_logs(1);
            state.history_visible = history.size() == 1 && history[0].message == record.message;
            (void)set_log_filter("*=trace");
            constexpr Log nested{"nested"}; nested.info("inner");
        }, &state);
        constexpr Log objects{"objects"}; objects.info("outer");
        check(state.history_visible);
        check(state.seen == std::vector<std::string>{"outer", "inner"});
        const auto history = recent_logs(8);
        check_eq(history.size(), std::size_t{2});
        if (history.size() == 2) check(history[0].message == "outer" && history[1].message == "inner");
    });
    suite.test("JSON lines retain the existing field vocabulary and escape control text", [] {
        const LoggedRecord record{.level = Level::warning, .category = "objects", .message = "quote\"\\\n\t",
                                  .file = "fixture.cpp", .line = 17, .unix_ms = 123, .thread = 9};
        const std::string expected = R"({"level":"warning","category":"objects","message":"quote\"\\\n\u0009","file":"fixture.cpp","line":17,"time":123,"thread":9})";
        check_eq(to_json(record), expected);
    });
    suite.test("a satisfied invariant is silent and preserves the logging policy", [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_level(Level::debug); set_log_sink(capture, &got);
        invariant(true, "satisfied independent invariant");
        check(got.entries.empty() && recent_logs().empty());
        check(log_level() == Level::debug);
    });
    return suite.run(argc, argv);
}
