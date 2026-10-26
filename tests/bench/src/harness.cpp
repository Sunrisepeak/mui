module openmui.bench;

import std;
import openmui.core;
import mcpplibs.os;

namespace omui::bench {

namespace {

std::atomic<std::uint64_t> allocation_count{0};
std::atomic<bool>          allocation_counting{false};

std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

double percentile(std::span<const double> sorted, double p) {
    const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

std::string platform_name() {
    namespace os = mcpplibs::os;
    if constexpr (os::is_windows) return "windows";
    else if constexpr (os::is_macos) return "macos";
    else if constexpr (os::is_linux) return "linux";
    else return "other";
}

std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    return std::format("{:.6g}", v);
}

}  // namespace

// What run_one needs of a State it drives.
struct Access {
    static bool timed(const State& s) noexcept { return s.done_ > s.config_.warmup; }
    static const std::vector<double>& samples(const State& s) noexcept { return s.samples_ms_; }
    static const std::vector<std::uint64_t>& allocations(const State& s) noexcept { return s.allocations_; }
    static std::uint64_t items(const State& s) noexcept { return s.items_; }
    static std::vector<std::pair<std::string, double>> counters(const State& s) {
        std::vector<std::pair<std::string, double>> out;
        for (std::size_t i = 0; i < s.counters_.size(); ++i)
            out.emplace_back(s.counters_[i].first, s.counters_[i].second / static_cast<double>(std::max<std::size_t>(1, s.counter_n_[i].second)));
        return out;
    }
};

bool State::next() {
    // Read the clock and the allocation count before the harness does anything of its own.
    const clock::time_point now = clock::now();
    const std::uint64_t allocs_now = allocation_count.load(std::memory_order_relaxed);
    if (running_) {
        if (paused_at_ != clock::time_point{}) {
            paused_ += now - paused_at_;
            allocs_paused_ += allocs_now - allocs_pause_at_;
            paused_at_ = {};
        }
        ++done_;
        if (done_ > config_.warmup) {
            const clock::duration d = now - started_ - paused_;
            measured_total_ += d;
            samples_ms_.push_back(std::chrono::duration<double, std::milli>(d).count());
            allocations_.push_back(allocs_now - allocs_at_ - allocs_paused_);
        }
        running_ = false;
    }
    const std::size_t timed = samples_ms_.size();
    const double elapsed = std::chrono::duration<double, std::milli>(measured_total_).count();
    if (timed >= config_.max_iterations) return false;
    if (timed >= config_.min_iterations && elapsed >= config_.min_time_ms) return false;
    running_ = true;
    paused_ = {};
    paused_at_ = {};
    allocs_paused_ = 0;
    allocs_at_ = allocation_count.load(std::memory_order_relaxed);
    started_ = clock::now();
    return true;
}

void State::pause() noexcept {
    if (!running_ || paused_at_ != clock::time_point{}) return;
    paused_at_ = clock::now();
    allocs_pause_at_ = allocation_count.load(std::memory_order_relaxed);
}

void State::resume() noexcept {
    if (paused_at_ == clock::time_point{}) return;
    paused_ += clock::now() - paused_at_;
    paused_at_ = {};
    allocs_paused_ += allocation_count.load(std::memory_order_relaxed) - allocs_pause_at_;
}

void State::counter(std::string_view name, double value) {
    if (done_ < config_.warmup) return;  // the iteration running is a warm-up one
    // The harness's own bookkeeping is not the body's: its allocations are not counted.
    const std::uint64_t before = allocation_count.load(std::memory_order_relaxed);
    struct Uncount {
        std::uint64_t  before;
        std::uint64_t& paused;
        ~Uncount() { paused += allocation_count.load(std::memory_order_relaxed) - before; }
    } uncount{before, allocs_paused_};
    for (std::size_t i = 0; i < counters_.size(); ++i)
        if (counters_[i].first == name) {
            counters_[i].second += value;
            ++counter_n_[i].second;
            return;
        }
    counters_.emplace_back(std::string(name), value);
    counter_n_.emplace_back(std::string(name), 1);
}

std::optional<double> Result::metric(std::string_view m) const {
    if (m == "min_ms") return min_ms;
    if (m == "mean_ms") return mean_ms;
    if (m == "p50_ms") return p50_ms;
    if (m == "p95_ms") return p95_ms;
    if (m == "p99_ms") return p99_ms;
    if (m == "max_ms") return max_ms;
    if (m == "ns_per_item") return items ? std::optional<double>(ns_per_item) : std::nullopt;
    if (m == "allocations") return allocations >= 0 ? std::optional<double>(allocations) : std::nullopt;
    if (m == "max_allocations") return allocations >= 0 ? std::optional<double>(static_cast<double>(max_allocations)) : std::nullopt;
    for (const auto& [k, v] : counters)
        if (k == m) return v;
    return std::nullopt;
}

void add(std::string name, std::string description, Body body) {
    registry().push_back(Case{std::move(name), std::move(description), std::move(body)});
}

std::span<const Case> cases() noexcept { return registry(); }

Result run_one(const Case& c, const Options& o) {
    State s({o.warmup, std::max<std::size_t>(1, o.min_iterations), std::max(o.min_iterations, o.max_iterations), o.min_time_ms});
    c.body(s);
    Result r;
    r.name = c.name;
    r.description = c.description;
    std::vector<double> ms = Access::samples(s);
    r.iterations = ms.size();
    if (ms.empty()) return r;
    std::ranges::sort(ms);
    r.min_ms = ms.front();
    r.max_ms = ms.back();
    r.mean_ms = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
    r.p50_ms = percentile(ms, 50);
    r.p95_ms = percentile(ms, 95);
    r.p99_ms = percentile(ms, 99);
    r.items = Access::items(s);
    if (r.items) r.ns_per_item = r.mean_ms * 1e6 / static_cast<double>(r.items);
    if (counting_allocations()) {
        const auto& a = Access::allocations(s);
        r.allocations = static_cast<double>(std::accumulate(a.begin(), a.end(), std::uint64_t{0})) / static_cast<double>(a.size());
        r.max_allocations = std::ranges::max(a);
    }
    r.counters = Access::counters(s);
    return r;
}

Report run(const Options& o) {
    Report report;
    report.label = o.label;
    report.platform = platform_name();
    report.hardware_threads = std::thread::hardware_concurrency();
    report.started = std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    for (const Case& c : cases())
        if (o.filter.empty() || c.name.contains(o.filter)) report.results.push_back(run_one(c, o));
    return report;
}

const Result* Report::find(std::string_view name) const noexcept {
    for (const Result& r : results)
        if (r.name == name) return &r;
    return nullptr;
}

std::string Report::json() const {
    Value v;
    v.set("format", "openmui.bench@0.1").set("label", label).set("platform", platform);
    v.set("hardware_threads", static_cast<std::int64_t>(hardware_threads)).set("started", started);
    Value::List list;
    for (const Result& r : results) {
        Value x;
        x.set("name", r.name).set("description", r.description).set("iterations", static_cast<std::int64_t>(r.iterations));
        for (auto [k, m] : {std::pair{"min_ms", r.min_ms}, {"mean_ms", r.mean_ms}, {"p50_ms", r.p50_ms}, {"p95_ms", r.p95_ms},
                            {"p99_ms", r.p99_ms}, {"max_ms", r.max_ms}})
            x.set(k, m);
        if (r.items) x.set("items", static_cast<std::int64_t>(r.items)).set("ns_per_item", r.ns_per_item);
        if (r.allocations >= 0) x.set("allocations", r.allocations).set("max_allocations", static_cast<std::int64_t>(r.max_allocations));
        Value c = Value(Value::Map{});
        for (const auto& [k, m] : r.counters) c.set(k, m);
        x.set("counters", std::move(c));
        list.push_back(std::move(x));
    }
    v.set("results", Value(std::move(list)));
    return v.json() + "\n";
}

std::string Report::table() const {
    std::string out = std::format("{:<34} {:>6} {:>9} {:>9} {:>9} {:>9} {:>10} {:>9}\n", "benchmark", "iters", "mean ms", "p50", "p95",
                                  "max", "ns/item", "allocs");
    for (const Result& r : results) {
        out += std::format("{:<34} {:>6} {:>9.3f} {:>9.3f} {:>9.3f} {:>9.3f} {:>10} {:>9}\n", r.name, r.iterations, r.mean_ms, r.p50_ms,
                           r.p95_ms, r.max_ms, r.items ? std::format("{:.1f}", r.ns_per_item) : std::string("-"),
                           r.allocations >= 0 ? std::format("{:.1f}", r.allocations) : std::string("-"));
        if (!r.counters.empty()) {
            std::string line;
            for (const auto& [k, m] : r.counters) line += std::format("{}{}={}", line.empty() ? "" : "  ", k, number(m));
            out += std::format("    {}\n", line);
        }
    }
    out += std::format("({}, {} hardware threads{}{})\n", platform, hardware_threads, label.empty() ? "" : ", ", label);
    return out;
}

std::vector<Violation> Report::check(std::span<const Budget> budgets, bool require_all) const {
    std::vector<Violation> out;
    for (const Budget& b : budgets) {
        const Result* r = find(b.name);
        if (!r) {
            if (require_all) out.push_back({b, std::numeric_limits<double>::quiet_NaN()});
            continue;
        }
        const std::optional<double> v = r->metric(b.metric);
        if (!v) {
            // allocations are only counted by a program that counts them
            if (require_all || (b.metric != "allocations" && b.metric != "max_allocations"))
                out.push_back({b, std::numeric_limits<double>::quiet_NaN()});
            continue;
        }
        if (*v > b.max) out.push_back({b, *v});
    }
    return out;
}

std::expected<std::vector<Budget>, std::string> parse_budgets(std::string_view text) {
    auto doc = parse_json(text);
    if (!doc) return std::unexpected(std::format("budgets: {}", doc.error().message));
    const Value* format = doc->get("format");
    if (!format || format->as_string() != "openmui.bench.budgets@0.1")
        return std::unexpected("budgets: \"format\" must be \"openmui.bench.budgets@0.1\"");
    const Value* list = doc->get("budgets");
    if (!list || !list->list()) return std::unexpected("budgets: \"budgets\" must be a list");
    std::vector<Budget> out;
    for (const Value& item : *list->list()) {
        Budget b;
        const Value* name = item.get("name");
        const Value* max = item.get("max");
        if (!name || name->as_string().empty() || !max || !max->is_number())
            return std::unexpected(std::format("budgets: every budget needs \"name\" and a numeric \"max\" (got {})", item.json()));
        b.name = std::string(name->as_string());
        b.max = max->as_double();
        if (const Value* m = item.get("metric")) b.metric = std::string(m->as_string());
        if (const Value* w = item.get("why")) b.why = std::string(w->as_string());
        if (b.why.empty()) return std::unexpected(std::format("budgets: `{}` must say why (\"why\")", b.name));
        out.push_back(std::move(b));
    }
    return out;
}

void count_allocation() noexcept {
    allocation_counting.store(true, std::memory_order_relaxed);
    allocation_count.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t allocations() noexcept { return allocation_count.load(std::memory_order_relaxed); }
bool counting_allocations() noexcept { return allocation_counting.load(std::memory_order_relaxed); }

}  // namespace omui::bench
