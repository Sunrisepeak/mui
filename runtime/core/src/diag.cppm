// openmui.core:diag — logging and invariants, without macros.
//
// The location of a message comes from std::source_location, captured by a
// format wrapper so the call reads like std::print:
//
//     omui::warn("font {} has no glyph for U+{:04X}", name, cp);
//
// An invariant that fails is a bug of openmui or of its caller: it is
// reported with its location and the program stops (core code does not throw).
export module openmui.core:diag;

import std;

export namespace omui {

enum class Level : std::uint8_t { trace, debug, info, warning, error };

[[nodiscard]] constexpr std::string_view name(Level l) noexcept {
    switch (l) {
        case Level::trace:   return "trace";
        case Level::debug:   return "debug";
        case Level::info:    return "info";
        case Level::warning: return "warning";
        case Level::error:   return "error";
    }
    return "info";
}

struct LogRecord {
    Level                level;
    std::string_view     message;
    std::source_location where;
    std::string_view     category = "omui";  // the part of openmui (or of the app) it is about: layout, text, agent …
};

// Where log records go. The default sink writes warnings and errors to
// standard error; a host, a test or the devtools replace it.
using LogSink = void (*)(void* user, const LogRecord& record);
void set_log_sink(LogSink sink, void* user) noexcept;
void set_log_level(Level minimum) noexcept;
[[nodiscard]] Level log_level() noexcept;

// Levels per category: "layout=debug,text=warning,*=info" (`*`: the rest).
// A host reads it from OMUI_LOG.
std::expected<void, std::string> set_log_filter(std::string_view spec);
[[nodiscard]] bool log_enabled(Level level, std::string_view category) noexcept;

void log(Level level, std::string_view message, std::source_location where = std::source_location::current());
void log(Level level, std::string_view category, std::string_view message, std::source_location where);

// The last records, kept for tools (`omui agent logs`, the devtools), with
// their time and thread; `set_log_history(0)` keeps none.
struct LoggedRecord {
    Level         level = Level::info;
    std::string   category;
    std::string   message;
    std::string   file;
    std::uint32_t line = 0;
    std::int64_t  unix_ms = 0;
    std::uint64_t thread = 0;
};
void set_log_history(std::size_t records);
[[nodiscard]] std::vector<LoggedRecord> recent_logs(std::size_t at_most = 200, Level at_least = Level::trace);
// One JSON object, for JSON-lines logs.
[[nodiscard]] std::string to_json(const LoggedRecord& record);

// A format string with the caller's location.
template <class... Args>
struct LogFormat {
    std::format_string<Args...> fmt;
    std::source_location        where;

    template <class S>
        requires std::convertible_to<const S&, std::string_view>
    consteval LogFormat(const S& s, std::source_location loc = std::source_location::current()) : fmt(s), where(loc) {}
};

// The level helpers, in the category "omui": omui::warn("…", …).
template <class... Args>
void verbose(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) {
    if (log_level() <= Level::trace) log(Level::trace, std::format(f.fmt, std::forward<Args>(args)...), f.where);
}
template <class... Args>
void debug(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) {
    if (log_level() <= Level::debug) log(Level::debug, std::format(f.fmt, std::forward<Args>(args)...), f.where);
}
template <class... Args>
void info(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) {
    if (log_level() <= Level::info) log(Level::info, std::format(f.fmt, std::forward<Args>(args)...), f.where);
}
template <class... Args>
void warn(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) {
    if (log_level() <= Level::warning) log(Level::warning, std::format(f.fmt, std::forward<Args>(args)...), f.where);
}
template <class... Args>
void error(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) {
    log(Level::error, std::format(f.fmt, std::forward<Args>(args)...), f.where);
}

// A category's log: `constexpr omui::Log log{"layout"}; log.debug("{} nodes", n);`
// The message is formatted only when the category's level lets it through.
struct Log {
    std::string_view category;

    template <class... Args>
    void at(Level level, LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const {
        if (log_enabled(level, category)) omui::log(level, category, std::format(f.fmt, std::forward<Args>(args)...), f.where);
    }
    template <class... Args>
    void verbose(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const { at<Args...>(Level::trace, f, std::forward<Args>(args)...); }
    template <class... Args>
    void debug(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const { at<Args...>(Level::debug, f, std::forward<Args>(args)...); }
    template <class... Args>
    void info(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const { at<Args...>(Level::info, f, std::forward<Args>(args)...); }
    template <class... Args>
    void warn(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const { at<Args...>(Level::warning, f, std::forward<Args>(args)...); }
    template <class... Args>
    void error(LogFormat<std::type_identity_t<Args>...> f, Args&&... args) const { at<Args...>(Level::error, f, std::forward<Args>(args)...); }
};

// Stops the program with a report when `ok` is false.
void invariant(bool ok, std::string_view what, std::source_location where = std::source_location::current());

[[noreturn]] void fatal(std::string_view what, std::source_location where = std::source_location::current());

}  // namespace omui
