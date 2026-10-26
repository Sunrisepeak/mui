module openmui.trace;

import std;

namespace omui::trace {

namespace {

// One thread's events in one session: a fixed array its thread fills and
// publishes by `size` (release); a reader copies up to `size` (acquire).
struct Buffer {
    std::unique_ptr<Event[]>  events;
    std::size_t               capacity = 0;
    std::atomic<std::size_t>  size{0};
    std::atomic<std::size_t>  dropped{0};
    std::uint32_t             thread = 0;
    std::string               name;
};

struct Session {
    std::mutex                           lock;
    std::vector<std::shared_ptr<Buffer>> buffers;
    std::atomic<std::uint64_t>           generation{0};
    clock::time_point                    started{};
    Options                              options;
    std::uint32_t                        next_thread = 1;
};

Session& session() {
    static Session s;
    return s;
}

struct Local {
    std::uint64_t           generation = 0;
    std::shared_ptr<Buffer> buffer;
    std::string             name;
};

Local& local() {
    thread_local Local l;
    return l;
}

Buffer* buffer_for_this_thread() {
    Session& s = session();
    Local& l = local();
    const std::uint64_t g = s.generation.load(std::memory_order_acquire);
    if (l.generation == g && l.buffer) return l.buffer.get();
    std::scoped_lock guard(s.lock);
    auto b = std::make_shared<Buffer>();
    b->capacity = s.options.events_per_thread;
    b->events = std::make_unique<Event[]>(b->capacity);
    b->thread = s.next_thread++;
    b->name = l.name;
    s.buffers.push_back(b);
    l.buffer = std::move(b);
    l.generation = g;
    return l.buffer.get();
}

std::string json_string(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else out += c;
        }
    }
    return out + "\"";
}

Trace collect(bool end) {
    Session& s = session();
    if (end) detail::on.store(false, std::memory_order_relaxed);
    Trace t;
    std::scoped_lock guard(s.lock);
    for (const auto& b : s.buffers) {
        const std::size_t n = b->size.load(std::memory_order_acquire);
        t.events.insert(t.events.end(), b->events.get(), b->events.get() + n);
        t.dropped += b->dropped.load(std::memory_order_relaxed);
        t.threads.emplace_back(b->thread, b->name.empty() ? std::format("thread {}", b->thread) : b->name);
    }
    std::ranges::stable_sort(t.events, {}, &Event::ns);
    return t;
}

}  // namespace

namespace detail {

std::atomic<bool> on{false};

void record(Kind kind, const char* name, const char* category, double value) noexcept {
    Buffer* b = buffer_for_this_thread();
    const std::size_t i = b->size.load(std::memory_order_relaxed);
    if (i >= b->capacity) {
        b->dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - session().started).count();
    b->events[i] = Event{kind, name, category, ns, value, b->thread};
    b->size.store(i + 1, std::memory_order_release);
}

}  // namespace detail

void name_thread(std::string name) {
    Local& l = local();
    l.name = name;
    if (l.buffer && l.generation == session().generation.load()) {
        std::scoped_lock guard(session().lock);
        l.buffer->name = std::move(name);
    }
}

void start(Options options) {
    Session& s = session();
    detail::on.store(false, std::memory_order_relaxed);
    {
        std::scoped_lock guard(s.lock);
        s.buffers.clear();
        s.options = options;
        s.next_thread = 1;
        s.started = clock::now();
        s.generation.fetch_add(1, std::memory_order_acq_rel);
    }
    detail::on.store(true, std::memory_order_relaxed);
}

Trace stop() { return collect(true); }
Trace snapshot() { return collect(false); }

std::string Trace::chrome_json() const {
    std::string out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    bool first = true;
    auto add = [&](std::string e) {
        if (!first) out += ',';
        first = false;
        out += e;
    };
    for (const auto& [id, name] : threads)
        add(std::format("{{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":{},\"args\":{{\"name\":{}}}}}", id, json_string(name)));
    for (const Event& e : events) {
        const double us = static_cast<double>(e.ns) / 1000.0;
        switch (e.kind) {
            case Kind::begin:
            case Kind::end:
                add(std::format("{{\"name\":{},\"cat\":{},\"ph\":\"{}\",\"ts\":{},\"pid\":1,\"tid\":{}}}", json_string(e.name),
                                json_string(e.category), e.kind == Kind::begin ? "B" : "E", us, e.thread));
                break;
            case Kind::counter:
                add(std::format("{{\"name\":{},\"cat\":{},\"ph\":\"C\",\"ts\":{},\"pid\":1,\"tid\":{},\"args\":{{\"value\":{}}}}}",
                                json_string(e.name), json_string(e.category), us, e.thread, e.value));
                break;
            case Kind::mark:
                add(std::format("{{\"name\":{},\"cat\":{},\"ph\":\"i\",\"s\":\"t\",\"ts\":{},\"pid\":1,\"tid\":{}}}", json_string(e.name),
                                json_string(e.category), us, e.thread));
                break;
        }
    }
    out += std::format("],\"otherData\":{{\"dropped\":{}}}}}", dropped);
    return out;
}

std::vector<Stat> Trace::summary() const {
    // Begin and end pair up per thread, innermost first.
    std::map<std::uint32_t, std::vector<const Event*>> open;
    std::map<std::pair<std::string_view, std::string_view>, std::vector<double>> spans;
    for (const Event& e : events) {
        if (e.kind == Kind::begin) open[e.thread].push_back(&e);
        else if (e.kind == Kind::end) {
            auto& stack = open[e.thread];
            if (stack.empty()) continue;
            const Event* b = stack.back();
            stack.pop_back();
            spans[{b->name, b->category}].push_back(static_cast<double>(e.ns - b->ns) / 1e6);
        }
    }
    std::vector<Stat> out;
    for (auto& [key, ds] : spans) {
        std::ranges::sort(ds);
        Stat s;
        s.name = std::string(key.first);
        s.category = std::string(key.second);
        s.count = ds.size();
        for (double d : ds) s.total_ms += d;
        s.mean_ms = s.total_ms / static_cast<double>(ds.size());
        auto pct = [&](double p) {
            const std::size_t rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(ds.size())));
            return ds[std::clamp<std::size_t>(rank, 1, ds.size()) - 1];
        };
        s.p50_ms = pct(50);
        s.p95_ms = pct(95);
        s.p99_ms = pct(99);
        s.max_ms = ds.back();
        out.push_back(std::move(s));
    }
    std::ranges::sort(out, std::greater{}, &Stat::total_ms);
    return out;
}

std::string Trace::table() const {
    std::string out = std::format("{:<28} {:>7} {:>10} {:>9} {:>9} {:>9} {:>9}\n", "span", "count", "total ms", "p50", "p95", "p99", "max");
    for (const Stat& s : summary())
        out += std::format("{:<28} {:>7} {:>10.3f} {:>9.3f} {:>9.3f} {:>9.3f} {:>9.3f}\n", s.category + "/" + s.name, s.count, s.total_ms,
                           s.p50_ms, s.p95_ms, s.p99_ms, s.max_ms);
    if (dropped) out += std::format("({} events dropped: raise Options::events_per_thread)\n", dropped);
    return out;
}

}  // namespace omui::trace
