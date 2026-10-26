module openmui.core;

import std;

namespace omui {

namespace {

void default_sink(void*, const LogRecord& r) {
    if (r.level < Level::warning) return;
    std::println(std::cerr, "openmui {}: {} ({}:{})", name(r.level), r.message, r.where.file_name(), r.where.line());
}

std::atomic<LogSink> sink_fn{default_sink};
std::atomic<void*> sink_user{nullptr};
std::atomic<Level> minimum{Level::info};

}  // namespace

void set_log_sink(LogSink sink, void* user) noexcept {
    sink_user.store(user);
    sink_fn.store(sink ? sink : default_sink);
}

void set_log_level(Level level) noexcept { minimum.store(level); }

Level log_level() noexcept { return minimum.load(std::memory_order_relaxed); }

void log(Level level, std::string_view message, std::source_location where) {
    if (level < log_level() && level != Level::error) return;
    sink_fn.load()(sink_user.load(), LogRecord{level, message, where});
}

void invariant(bool ok, std::string_view what, std::source_location where) {
    if (!ok) fatal(what, where);
}

void fatal(std::string_view what, std::source_location where) {
    std::println(std::cerr, "openmui: invariant failed: {} ({}:{} in {})", what, where.file_name(), where.line(),
                 where.function_name());
    std::abort();
}

}  // namespace omui
