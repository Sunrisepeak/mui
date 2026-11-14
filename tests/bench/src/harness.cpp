module omui.bench;

import std;
import omui.core;
import mcpplibs.mtest.bench;

namespace omui::bench {

namespace {

namespace measure = mcpplibs::mtest::bench;

std::vector<Case>& registry() {
    static std::vector<Case> all;
    return all;
}

// The value at percentile `percent` of sorted samples, nearest rank: what the budgets were set against.
double percentile(std::span<const double> sorted, double percent) {
    const auto rank = static_cast<std::size_t>(std::ceil(percent / 100.0 * static_cast<double>(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

}  // namespace

struct State::Impl {
    Options                     options;
    std::optional<measure::laps> laps;  // made at the first next(): a body that skips first measures nothing
    bool                        running = false;  // an iteration is being timed
    // What the body said outside an iteration (how many items an iteration works through, said before
    // the loop; a figure worked out after it): the result's, as what an iteration says.
    std::uint64_t                               items = 0;
    std::vector<std::pair<std::string, double>> said_outside;
    std::string                 skipped, failed;
    std::vector<std::pair<std::string, std::string>> notes;
};

struct Access {
    static State::Impl& of(State& state) noexcept { return *state.impl_; }
};

State::State(const Options& options) : impl_(std::make_unique<Impl>()) { impl_->options = options; }
State::~State() = default;

bool State::next() {
    Impl& d = *impl_;
    if (!d.skipped.empty() || !d.failed.empty()) return false;
    if (!d.laps)
        d.laps.emplace("iteration", measure::each_options{.least = std::max<std::size_t>(1, d.options.min_iterations),
                                                           .most = std::max(d.options.min_iterations, d.options.max_iterations),
                                                           .time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                               std::chrono::duration<double, std::milli>(d.options.min_time_ms)),
                                                           .warmup = d.options.warmup});
    d.running = d.laps->next();
    return d.running;
}

void State::pause() noexcept {
    if (impl_->laps) impl_->laps->pause();
}
void State::resume() noexcept {
    if (impl_->laps) impl_->laps->resume();
}
void State::items(std::uint64_t n) noexcept { impl_->items = n; }
void State::counter(std::string_view name, double value) {
    Impl& d = *impl_;
    if (d.running) {
        d.laps->value(name, value);
        return;
    }
    for (auto& [known, last] : d.said_outside)
        if (known == name) {
            last = value;
            return;
        }
    d.said_outside.emplace_back(std::string(name), value);
}
std::size_t State::iteration() const noexcept { return impl_->laps ? impl_->laps->index() : 0; }

void State::note(std::string_view name, std::string text) {
    for (auto& [known, said] : impl_->notes)
        if (known == name) {
            said = std::move(text);
            return;
        }
    impl_->notes.emplace_back(std::string(name), std::move(text));
}

void State::skip(std::string reason) { impl_->skipped = reason.empty() ? std::string("skipped") : std::move(reason); }

void State::fail(std::string reason) {
    if (impl_->failed.empty()) impl_->failed = reason.empty() ? std::string("failed") : std::move(reason);
}

std::optional<double> Result::metric(std::string_view m) const {
    if (!skipped.empty() || !failed.empty()) return std::nullopt;
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

Result run_one(const Case& c, const Options& options) {
    State state(options);
    c.body(state);
    auto& d = Access::of(state);
    Result result;
    result.name = c.name;
    result.description = c.description;
    result.notes = d.notes;
    if (!d.skipped.empty()) {
        result.skipped = d.skipped;
        return result;
    }
    if (!d.failed.empty()) {
        result.failed = d.failed;
        return result;
    }
    if (!d.laps) return result;
    // A body that left its loop early (it returned): what was measured so far is not a measurement.
    while (d.laps->next()) {
        result.failed = "the body left its loop before the measurement ended";
        d.laps->last();
    }
    if (!result.failed.empty()) return result;
    const measure::distribution& lie = d.laps->result();
    std::vector<double> ms;
    ms.reserve(lie.samples.size());
    for (const double seconds : lie.samples) ms.push_back(seconds * 1000.0);
    result.iterations = ms.size();
    result.samples_ms = ms;
    if (ms.empty()) return result;
    std::ranges::sort(ms);
    result.min_ms = ms.front();
    result.max_ms = ms.back();
    result.mean_ms = std::accumulate(ms.begin(), ms.end(), 0.0) / static_cast<double>(ms.size());
    result.p50_ms = percentile(ms, 50);
    result.p95_ms = percentile(ms, 95);
    result.p99_ms = percentile(ms, 99);
    result.items = d.items;
    if (result.items) result.ns_per_item = result.mean_ms * 1e6 / static_cast<double>(result.items);
    for (const measure::quantity& said : lie.values) {
        // What the program counted of its allocations (mtest's names) under the budgets' names; the rest are the body's counters.
        if (said.name == "allocations") {
            result.allocations = said.mean;
            result.max_allocations = static_cast<std::uint64_t>(said.max);
        } else if (said.name != "allocated") {
            result.counters.emplace_back(said.name, said.mean);
        }
    }
    for (const auto& [name, value] : d.said_outside) {
        const auto known = std::ranges::find(result.counters, name, &std::pair<std::string, double>::first);
        if (known != result.counters.end()) known->second = value;
        else result.counters.emplace_back(name, value);
    }
    return result;
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
        if (const Value* u = item.get("unless")) b.unless = std::string(u->as_string());
        if (b.why.empty()) return std::unexpected(std::format("budgets: `{}` must say why (\"why\")", b.name));
        out.push_back(std::move(b));
    }
    return out;
}

}  // namespace omui::bench
