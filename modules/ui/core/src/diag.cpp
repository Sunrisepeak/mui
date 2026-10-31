module openmui.core;

import std;

namespace omui {

namespace {

void default_sink(void*, const LogRecord& r) {
    if (r.level < Level::warning) return;
    std::println(std::cerr, "openmui {} [{}]: {} ({}:{})", name(r.level), r.category, r.message, r.where.file_name(), r.where.line());
}

std::atomic<LogSink> sink_fn{default_sink};
std::atomic<void*> sink_user{nullptr};
// The least level any category lets through: the fast check before formatting.
std::atomic<Level> minimum{Level::info};

struct Filter {
    std::shared_mutex                                     lock;
    Level                                                 rest = Level::info;  // `*`
    std::vector<std::pair<std::string, Level>>            categories;
};

Filter& filter() {
    static Filter f;
    return f;
}

struct History {
    std::mutex                lock;
    std::deque<LoggedRecord>  records;
    std::size_t               capacity = 512;
};

History& history() {
    static History h;
    return h;
}

std::optional<Level> parse_level(std::string_view s) {
    for (Level l : {Level::trace, Level::debug, Level::info, Level::warning, Level::error})
        if (name(l) == s) return l;
    if (s == "warn") return Level::warning;
    if (s == "off") return Level::error;
    return std::nullopt;
}

void refresh_minimum() {
    Filter& f = filter();
    Level least = f.rest;
    for (const auto& [c, l] : f.categories) least = std::min(least, l);
    minimum.store(least);
}

std::string json_text(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        else out += c;
    }
    return out + '"';
}

}  // namespace

void set_log_sink(LogSink sink, void* user) noexcept {
    sink_user.store(user);
    sink_fn.store(sink ? sink : default_sink);
}

void set_log_level(Level level) noexcept {
    Filter& f = filter();
    std::unique_lock g(f.lock);
    f.rest = level;
    f.categories.clear();
    refresh_minimum();
}

Level log_level() noexcept { return minimum.load(std::memory_order_relaxed); }

std::expected<void, std::string> set_log_filter(std::string_view spec) {
    Level rest = Level::info;
    std::vector<std::pair<std::string, Level>> categories;
    while (!spec.empty()) {
        const auto comma = spec.find(',');
        std::string_view item = spec.substr(0, comma);
        spec = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
        while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ') item.remove_suffix(1);
        if (item.empty()) continue;
        const auto eq = item.find('=');
        const std::string_view cat = eq == std::string_view::npos ? "*" : item.substr(0, eq);
        const std::string_view lvl = eq == std::string_view::npos ? item : item.substr(eq + 1);
        auto l = parse_level(lvl);
        if (!l) return std::unexpected(std::format("`{}` is not a level (trace, debug, info, warning, error, off)", lvl));
        if (cat == "*") rest = *l;
        else categories.emplace_back(std::string(cat), *l);
    }
    Filter& f = filter();
    std::unique_lock g(f.lock);
    f.rest = rest;
    f.categories = std::move(categories);
    refresh_minimum();
    return {};
}

bool log_enabled(Level level, std::string_view category) noexcept {
    if (level == Level::error) return true;
    if (level < minimum.load(std::memory_order_relaxed)) return false;
    Filter& f = filter();
    std::shared_lock g(f.lock);
    for (const auto& [c, l] : f.categories)
        if (c == category) return level >= l;
    return level >= f.rest;
}

void log(Level level, std::string_view message, std::source_location where) { log(level, "omui", message, where); }

void log(Level level, std::string_view category, std::string_view message, std::source_location where) {
    if (!log_enabled(level, category)) return;
    {
        History& h = history();
        std::scoped_lock g(h.lock);
        if (h.capacity > 0) {
            if (h.records.size() >= h.capacity) h.records.pop_front();
            h.records.push_back(LoggedRecord{
                level, std::string(category), std::string(message), std::string(where.file_name()), static_cast<std::uint32_t>(where.line()),
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(),
                static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))});
        }
    }
    sink_fn.load()(sink_user.load(), LogRecord{level, message, where, category});
}

void set_log_history(std::size_t records) {
    History& h = history();
    std::scoped_lock g(h.lock);
    h.capacity = records;
    while (h.records.size() > records) h.records.pop_front();
}

std::vector<LoggedRecord> recent_logs(std::size_t at_most, Level at_least) {
    History& h = history();
    std::scoped_lock g(h.lock);
    std::vector<LoggedRecord> out;
    for (auto it = h.records.rbegin(); it != h.records.rend() && out.size() < at_most; ++it)
        if (it->level >= at_least) out.push_back(*it);
    std::ranges::reverse(out);
    return out;
}

std::string to_json(const LoggedRecord& r) {
    return std::format("{{\"level\":\"{}\",\"category\":{},\"message\":{},\"file\":{},\"line\":{},\"time\":{},\"thread\":{}}}",
                       name(r.level), json_text(r.category), json_text(r.message), json_text(r.file), r.line, r.unix_ms, r.thread);
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
