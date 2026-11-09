// The public diagnostics API is independently consumable without UI/core.
import std;
import mlibs.log;
import mcpplibs.mtest;

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
using namespace mcpplibs::mtest;

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

UTest cases{"log.public", [] {
    "independent category sink preserves formatted text and the caller location"_test = [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_level(Level::trace); set_log_sink(capture, &got);
        constexpr Log objects{"hanim.object"};
        const auto call = std::source_location::current();
        objects.info("prepared {} objects", 12);
        expect(eq(got.entries.size(), std::size_t{1}));
        if (got.entries.empty()) return;
        expect(got.entries[0].level == Level::info && got.entries[0].category == "hanim.object");
        expect(eq(got.entries[0].message, std::string("prepared 12 objects")));
        expect(eq(got.entries[0].file, std::string(call.file_name())));
        expect(eq(got.entries[0].line, call.line() + 1));
    };
    "category filters retain the previous policy after an invalid update and errors always pass"_test = [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_sink(capture, &got);
        expect(set_log_filter(" objects=debug,render=error,*=warning ").has_value());
        expect(log_level() == Level::debug);
        expect(!set_log_filter("objects=loud").has_value());
        constexpr Log objects{"objects"}, render{"render"};
        objects.debug("accepted"); objects.verbose("filtered"); render.warn("filtered");
        render.error("always"); info("filtered"); warn("fallback");
        expect(eq(got.entries.size(), std::size_t{3}));
        if (got.entries.size() != 3) return;
        expect(eq(got.entries[0].message, std::string("accepted")));
        expect(eq(got.entries[1].message, std::string("always")));
        expect(eq(got.entries[2].message, std::string("fallback")));
        expect(set_log_filter("*=off").has_value());
        expect(!log_enabled(Level::warning, "objects") && log_enabled(Level::error, "objects"));
    };
    "bounded history owns its data and filters copies in chronological order"_test = [] {
        Restore restore;
        set_log_sink(quiet, nullptr); set_log_level(Level::trace); set_log_history(0); set_log_history(3);
        constexpr Log objects{"objects"};
        for (int i = 0; i < 5; ++i) objects.info("object {}", i);
        objects.warn("last warning");
        auto history = recent_logs(10);
        expect(eq(history.size(), std::size_t{3}));
        if (history.size() != 3) return;
        expect(eq(history[0].message, std::string("object 3")));
        expect(eq(history[1].message, std::string("object 4")));
        expect(eq(history[2].message, std::string("last warning")));
        expect(history[2].line > 0 && history[2].unix_ms > 0 && !history[2].file.empty());
        expect(eq(recent_logs(10, Level::warning).size(), std::size_t{1}));
        history.back().message = "changed caller copy";
        expect(eq(recent_logs(1).back().message, std::string("last warning")));
        set_log_history(1); expect(eq(recent_logs().size(), std::size_t{1}));
        set_log_history(0); objects.error("not retained"); expect(recent_logs().empty());
    };
    "sink reentry can inspect history change filters and emit an inner record"_test = [] {
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
        expect(state.history_visible);
        expect(state.seen == std::vector<std::string>{"outer", "inner"});
        const auto history = recent_logs(8);
        expect(eq(history.size(), std::size_t{2}));
        if (history.size() == 2) expect(history[0].message == "outer" && history[1].message == "inner");
    };
    "JSON lines retain the existing field vocabulary and escape control text"_test = [] {
        const LoggedRecord record{.level = Level::warning, .category = "objects", .message = "quote\"\\\n\t",
                                  .file = "fixture.cpp", .line = 17, .unix_ms = 123, .thread = 9};
        const std::string expected = R"({"level":"warning","category":"objects","message":"quote\"\\\n\u0009","file":"fixture.cpp","line":17,"time":123,"thread":9})";
        expect(eq(to_json(record), expected));
    };
    "a satisfied invariant is silent and preserves the logging policy"_test = [] {
        Restore restore;
        Capture got;
        set_log_history(0); set_log_level(Level::debug); set_log_sink(capture, &got);
        invariant(true, "satisfied independent invariant");
        expect(got.entries.empty() && recent_logs().empty());
        expect(log_level() == Level::debug);
    };
}};
