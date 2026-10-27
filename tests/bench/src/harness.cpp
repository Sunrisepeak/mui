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

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// What a program prints, its first line; empty when it is not there or fails.
std::string first_line_of(std::string program, std::vector<std::string> args) {
    namespace os = mcpplibs::os;
    os::process::spawn_options o;
    o.program = std::move(program);
    o.args = std::move(args);
    o.in = os::process::stdio::null;
    auto out = os::process::run(std::move(o));
    if (!out || !out->status.success()) return {};
    return trimmed(std::string_view(out->out).substr(0, out->out.find('\n')));
}

// `key : value' lines (lscpu's; os-release's with '='): the first value of `key'.
std::string field(std::string_view text, std::string_view key, char sep) {
    for (const auto line : std::views::split(text, '\n')) {
        const std::string_view l(line.begin(), line.end());
        const auto at = l.find(sep);
        if (at == std::string_view::npos || trimmed(l.substr(0, at)) != key) continue;
        std::string v = trimmed(l.substr(at + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

std::string cpu_model() {
    namespace os = mcpplibs::os;
    if (auto v = os::env::get("OMUI_BENCH_CPU"); v && !v->empty()) return *v;
    if constexpr (os::is_linux) {
        // lscpu names x86's model as the processor gives it, and Arm's from its part number.
        auto out = os::process::run({.program = "lscpu", .env = {{"LC_ALL", "C"}}, .in = os::process::stdio::null});
        if (!out || !out->status.success()) return {};
        const std::string vendor = field(out->out, "Vendor ID", ':'), model = field(out->out, "Model name", ':');
        if (model.empty() || model.starts_with(vendor)) return model;
        return vendor == "GenuineIntel" || vendor == "AuthenticAMD" ? model : std::format("{} {}", vendor, model);
    } else if constexpr (os::is_macos) {
        return first_line_of("sysctl", {"-n", "machdep.cpu.brand_string"});
    } else if constexpr (os::is_windows) {
        return os::env::get("PROCESSOR_IDENTIFIER").value_or("");
    } else {
        return {};
    }
}

std::string system_version() {
    namespace os = mcpplibs::os;
    if constexpr (os::is_linux) {
        std::string name;
        for (const char* f : {"/etc/os-release", "/usr/lib/os-release"})
            if (auto text = os::fs::read(f); text && name.empty()) name = field(*text, "PRETTY_NAME", '=');
        const std::string kernel = first_line_of("uname", {"-r"});
        if (name.empty()) name = "Linux";
        return kernel.empty() ? name : std::format("{}, kernel {}", name, kernel);
    } else if constexpr (os::is_macos) {
        const std::string v = first_line_of("sw_vers", {"-productVersion"});
        return v.empty() ? "macOS" : std::format("macOS {}", v);
    } else if constexpr (os::is_windows) {
        const std::string v = first_line_of("cmd", {"/c", "ver"});
        return v.empty() ? "Windows" : v;
    } else {
        return platform_name();
    }
}

// A Markdown table cell: no bars, no line breaks.
std::string cell(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

std::string metric_value(std::string_view metric, double v) {
    if (metric.ends_with("_ms")) return v < 1 ? std::format("{:.3f} ms", v) : std::format("{:.2f} ms", v);
    return number(v);
}

// A limit as it was written: 250 ms, 0.5 ms, 16.67 ms.
std::string limit_value(std::string_view metric, double v) {
    if (metric.ends_with("_ms")) return std::format("{} ms", number(v));
    if (metric.contains("allocations")) return std::format("{:.0f}", v);
    return number(v);
}

}  // namespace

// What run_one needs of a State it drives.
struct Access {
    static bool timed(const State& s) noexcept { return s.done_ > s.config_.warmup; }
    static const std::vector<double>& samples(const State& s) noexcept { return s.samples_ms_; }
    static const std::vector<std::uint64_t>& allocations(const State& s) noexcept { return s.allocations_; }
    static std::uint64_t items(const State& s) noexcept { return s.items_; }
    static const std::string& skipped(const State& s) noexcept { return s.skipped_; }
    static const std::vector<std::pair<std::string, std::string>>& notes(const State& s) noexcept { return s.notes_; }
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
    if (!skipped_.empty()) return false;
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

void State::note(std::string_view name, std::string text) {
    for (auto& [k, v] : notes_)
        if (k == name) {
            v = std::move(text);
            return;
        }
    notes_.emplace_back(std::string(name), std::move(text));
}

void State::skip(std::string reason) {
    skipped_ = reason.empty() ? std::string("skipped") : std::move(reason);
    running_ = false;
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
    if (!skipped.empty()) return std::nullopt;
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
    r.notes = Access::notes(s);
    if (!Access::skipped(s).empty()) {
        r.skipped = Access::skipped(s);
        return r;
    }
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
    report.machine = this_machine();
    report.options = o;
    for (const Case& c : cases())
        if (o.filter.empty() || c.name.contains(o.filter)) report.results.push_back(run_one(c, o));
    return report;
}

Machine this_machine() {
    namespace os = mcpplibs::os;
    return Machine{cpu_model(), os::machine::arch(), system_version(), os::machine::memory()};
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
    Value m;
    m.set("cpu", machine.cpu).set("arch", machine.arch).set("system", machine.system);
    if (machine.memory) m.set("memory", static_cast<std::int64_t>(*machine.memory));
    v.set("machine", std::move(m));
    Value::List list;
    for (const Result& r : results) {
        Value x;
        x.set("name", r.name).set("description", r.description).set("iterations", static_cast<std::int64_t>(r.iterations));
        if (!r.notes.empty()) {
            Value n = Value(Value::Map{});
            for (const auto& [k, t] : r.notes) n.set(k, t);
            x.set("notes", std::move(n));
        }
        if (!r.skipped.empty()) {
            x.set("skipped", r.skipped);
            list.push_back(std::move(x));
            continue;
        }
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
        if (!r.skipped.empty()) {
            out += std::format("{:<34} skipped: {}\n", r.name, r.skipped);
            continue;
        }
        out += std::format("{:<34} {:>6} {:>9.3f} {:>9.3f} {:>9.3f} {:>9.3f} {:>10} {:>9}\n", r.name, r.iterations, r.mean_ms, r.p50_ms,
                           r.p95_ms, r.max_ms, r.items ? std::format("{:.1f}", r.ns_per_item) : std::string("-"),
                           r.allocations >= 0 ? std::format("{:.1f}", r.allocations) : std::string("-"));
        if (!r.counters.empty() || !r.notes.empty()) {
            std::string line;
            for (const auto& [k, m] : r.counters) line += std::format("{}{}={}", line.empty() ? "" : "  ", k, number(m));
            for (const auto& [k, t] : r.notes) line += std::format("{}{}: {}", line.empty() ? "" : "  ", k, t);
            out += std::format("    {}\n", line);
        }
    }
    out += std::format("({}, {} hardware threads{}{})\n", platform, hardware_threads, label.empty() ? "" : ", ", label);
    return out;
}

namespace {

// The report's own words, in each language the docs are written in
// (docs/en, docs/zh); the benchmarks' descriptions are as the code gives them.
struct Words {
    std::string_view title, measured, fixed, machine, processor, not_reported, threads, arch, memory, system, gpu, method, method_text,
        results, header, skipped, item, why, unless, notes;
};

constexpr Words english{
    "# openmui benchmarks",
    "Measured {}{}.",
    " Every number is the wall-clock time of one iteration on the machine below; a GPU benchmark's includes submitting the "
    "frame and waiting for the GPU to finish it. `omui-bench --report` wrote this page from the same run as its JSON (`--json`).",
    "## The machine",
    "processor",
    "(not reported)",
    "hardware threads",
    "architecture",
    "memory",
    "system",
    "GPU",
    "## The method",
    "Each benchmark runs {} warm-up iterations, then at least {} timed ones and at least {} ms of them (at most {}). The "
    "percentiles are of the timed iterations, nearest rank. Time a body pauses (building its input) is not counted. Heap "
    "allocations are counted by the program's own `operator new`: the libraries measured are the ones every application "
    "links. Release profile; the CPU renderer uses the task pool (every hardware thread). A budget is a limit CI holds each "
    "change to (`--check`), with the reason it is there; one marked *unless software* is a GPU's, not held on a software "
    "device (lavapipe).",
    "## The results",
    "| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |",
    "skipped: {}",
    " {:.1f} ns an item.",
    "## Why each budget is there",
    "unless {}",
    "## Notes",
};

constexpr Words chinese{
    "# openmui 基准报告",
    "测量于 {}{}。",
    "每个数字都是下面这台机器上一次迭代的墙钟时间；GPU 基准的时间包括提交一帧并等待 GPU 完成。本页由 `omui-bench --report` "
    "从生成 JSON（`--json`）的同一次运行写出。",
    "## 机器",
    "处理器",
    "（未报告）",
    "硬件线程",
    "架构",
    "内存",
    "系统",
    "GPU",
    "## 方法",
    "每个基准先预热 {} 次迭代，再计时至少 {} 次、至少 {} 毫秒（最多 {} 次）。百分位数取计时迭代的最近秩。基准主体暂停的时间"
    "（准备输入）不计入。堆分配由程序自己的 `operator new` 计数：被测的库就是每个应用链接的库。release 配置；CPU 渲染器使用任务池"
    "（全部硬件线程）。预算是 CI 对每次修改检查的上限（`--check`），并写明设立的理由；标为“软件设备除外”的是 GPU 的预算，"
    "不用于软件设备（lavapipe）。",
    "## 结果",
    "| 基准 | 迭代 | p50 | p95 | p99 | 最大 | 分配 | 预算 | |",
    "跳过：{}",
    " 每项 {:.1f} ns。",
    "## 每项预算的理由",
    "{}除外",
    "## 说明",
};

std::string unless_text(const Words& w, std::string_view counter) {
    if (&w == &chinese && counter == "software") return "软件设备除外";
    return std::vformat(w.unless, std::make_format_args(counter));
}

}  // namespace

std::string Report::markdown(std::span<const Budget> budgets, std::string_view language) const {
    const Words& w = language == "zh" ? chinese : english;
    std::string out = std::format("{}\n\n", w.title);
    const std::string where = label.empty() ? "" : std::format(" ({})", cell(label));
    out += std::vformat(w.measured, std::make_format_args(started, where));
    out += std::format("{}\n\n", w.fixed);
    out += std::format("{}\n\n| | |\n|---|---|\n", w.machine);
    out += std::format("| {} | {} |\n", w.processor, machine.cpu.empty() ? std::string(w.not_reported) : cell(machine.cpu));
    out += std::format("| {} | {} |\n", w.threads, hardware_threads);
    out += std::format("| {} | {} |\n", w.arch, cell(machine.arch));
    if (machine.memory) out += std::format("| {} | {:.1f} GiB |\n", w.memory, static_cast<double>(*machine.memory) / (1024.0 * 1024 * 1024));
    out += std::format("| {} | {} |\n", w.system, cell(machine.system));
    std::vector<std::string> gpus;
    for (const Result& r : results)
        for (const auto& [k, t] : r.notes)
            if (k == "device" && std::ranges::find(gpus, t) == gpus.end()) gpus.push_back(t);
    for (const std::string& g : gpus) out += std::format("| {} | {} |\n", w.gpu, cell(g));
    if (const auto& notes = &w == &chinese ? options.notes_zh : options.notes; !notes.empty()) {
        out += std::format("\n{}\n\n", w.notes);
        for (const std::string& n : notes) out += std::format("- {}\n", n);
    }
    out += std::format("\n{}\n\n", w.method);
    out += std::vformat(w.method_text, std::make_format_args(options.warmup, options.min_iterations, options.min_time_ms, options.max_iterations));
    out += "\n\n";
    // By group: the name up to its first '/'.
    std::vector<std::string> groups;
    for (const Result& r : results) {
        const std::string g = r.name.substr(0, r.name.find('/'));
        if (std::ranges::find(groups, g) == groups.end()) groups.push_back(g);
    }
    out += std::format("{}\n", w.results);
    for (const std::string& g : groups) {
        out += std::format("\n### {}\n\n{}\n|---|---:|---:|---:|---:|---:|---:|---|---|\n", g, w.header);
        for (const Result& r : results) {
            if (r.name.substr(0, r.name.find('/')) != g) continue;
            std::string limits, verdict;
            for (const Budget& b : budgets) {
                if (b.name != r.name) continue;
                const std::optional<double> v = r.metric(b.metric);
                const bool exempt = !b.unless.empty() && r.metric(b.unless).value_or(0) != 0;
                limits += std::format("{}{} ≤ {}{}", limits.empty() ? "" : "<br>", b.metric, limit_value(b.metric, b.max),
                                      b.unless.empty() ? "" : std::format(" ({})", unless_text(w, b.unless)));
                verdict += std::format("{}{}", verdict.empty() ? "" : "<br>",
                                       !r.skipped.empty() ? std::string("—")
                                       : exempt           ? std::format("— ({})", b.unless)
                                       : !v               ? std::string("?")
                                       : *v <= b.max      ? std::string("✓")
                                                          : std::string("✗"));
            }
            if (!r.skipped.empty()) {
                const std::string why = cell(r.skipped);
                out += std::format("| `{}` | {} | | | | | | {} | {} |\n", r.name, std::vformat(w.skipped, std::make_format_args(why)), limits, verdict);
                continue;
            }
            out += std::format("| `{}` | {} | {} | {} | {} | {} | {} | {} | {} |\n", r.name, r.iterations, metric_value("p50_ms", r.p50_ms),
                               metric_value("p95_ms", r.p95_ms), metric_value("p99_ms", r.p99_ms), metric_value("max_ms", r.max_ms),
                               r.allocations >= 0 ? std::format("{:.1f}", r.allocations) : std::string("—"), limits, verdict);
        }
        out += "\n";
        for (const Result& r : results) {
            if (r.name.substr(0, r.name.find('/')) != g) continue;
            std::string extra;
            if (r.items && r.skipped.empty()) extra += std::vformat(w.item, std::make_format_args(r.ns_per_item));
            for (const auto& [k, m] : r.counters) extra += std::format(" `{}` = {}.", k, number(m));
            for (const auto& [k, t] : r.notes) extra += std::format(" {}: {}.", k, cell(t));
            std::string_view d = r.description;
            while (d.ends_with('.')) d.remove_suffix(1);
            out += std::format("- `{}`: {}.{}\n", r.name, cell(d), extra);
        }
    }
    out += std::format("\n{}\n\n", w.why);
    for (const Budget& b : budgets) {
        if (!find(b.name)) continue;
        out += std::format("- `{}` {} ≤ {}{}: {}\n", b.name, b.metric, limit_value(b.metric, b.max),
                           b.unless.empty() ? "" : std::format(" ({})", unless_text(w, b.unless)), cell(b.why));
    }
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
        if (!b.unless.empty() && r->metric(b.unless).value_or(0) != 0) continue;
        if (!r->skipped.empty()) {
            // a benchmark that cannot run here (no GPU) is not over its budget, unless every budget must be met
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
        if (const Value* u = item.get("unless")) b.unless = std::string(u->as_string());
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
