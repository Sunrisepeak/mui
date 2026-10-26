// openmui.trace — where the time goes.
//
//     omui::trace::start();
//     {
//         omui::trace::Span s("layout");          // a span: begin now, end at scope exit
//         ...
//     }
//     omui::trace::count("nodes", 1200);          // a counter's value
//     omui::trace::mark("frame");                 // an instant
//     auto t = omui::trace::stop();
//     t.chrome_json();                            // open in Perfetto or chrome://tracing
//     t.summary();                                // count, total, p50 / p95 / p99, max per span
//
// Recording is per thread, into buffers set aside when a thread first
// records: no lock and no allocation per event. While tracing is off a span
// costs one relaxed atomic load. Names are string literals (they are kept by
// pointer); categories group them ("frame", "layout", "render" …).
export module openmui.trace;

import std;

export namespace omui::trace {

using clock = std::chrono::steady_clock;

enum class Kind : std::uint8_t { begin, end, counter, mark };

struct Event {
    Kind              kind = Kind::mark;
    const char*       name = "";
    const char*       category = "";
    std::int64_t      ns = 0;      // since the session started
    double            value = 0;   // counters
    std::uint32_t     thread = 0;
};

struct Options {
    std::size_t events_per_thread = 1 << 16;  // a thread's buffer; when full, the oldest are kept and the rest dropped
};

namespace detail {
extern std::atomic<bool> on;
void record(Kind kind, const char* name, const char* category, double value) noexcept;
}  // namespace detail

[[nodiscard]] inline bool enabled() noexcept { return detail::on.load(std::memory_order_relaxed); }

// A span: from its construction to the end of its scope.
class Span {
public:
    explicit Span(const char* name, const char* category = "app") noexcept : name_(name), category_(category) {
        if (enabled()) {
            detail::record(Kind::begin, name_, category_, 0);
            open_ = true;
        }
    }
    ~Span() {
        if (open_) detail::record(Kind::end, name_, category_, 0);
    }
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;

private:
    const char* name_;
    const char* category_;
    bool        open_ = false;
};

inline void count(const char* name, double value, const char* category = "app") noexcept {
    if (enabled()) detail::record(Kind::counter, name, category, value);
}

inline void mark(const char* name, const char* category = "app") noexcept {
    if (enabled()) detail::record(Kind::mark, name, category, 0);
}

// Names a thread in the trace ("ui", "render", "worker 3").
void name_thread(std::string name);

struct Stat {
    std::string  name;
    std::string  category;
    std::size_t  count = 0;
    double       total_ms = 0, mean_ms = 0, p50_ms = 0, p95_ms = 0, p99_ms = 0, max_ms = 0;
};

struct Trace {
    std::vector<Event>                                  events;   // by time
    std::vector<std::pair<std::uint32_t, std::string>>  threads;  // id, name
    std::size_t                                         dropped = 0;

    // Chrome trace event format (JSON object form): Perfetto and chrome://tracing read it.
    [[nodiscard]] std::string chrome_json() const;
    // Per span name: how often, how long (begin–end pairs per thread).
    [[nodiscard]] std::vector<Stat> summary() const;
    // The summary as a table to read.
    [[nodiscard]] std::string table() const;
};

// Begins a session (clearing what a previous one left); recording is on.
void start(Options options = {});
// Ends it and hands over what was recorded.
[[nodiscard]] Trace stop();
// What has been recorded so far, without ending the session.
[[nodiscard]] Trace snapshot();

}  // namespace omui::trace
