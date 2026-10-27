// omui-bench — runs openmui's benchmarks, writes the report, checks budgets.
//
//   omui-bench                                    every benchmark, the table on stdout
//   omui-bench --filter ui/frame                  the ones whose name contains it
//   omui-bench --json results.bench.omui.json     also the report as JSON
//   omui-bench --check budgets.bench.omui.json    exit 1 when a budget is exceeded
//   omui-bench --report report.md                 also the report for people, in Markdown
//                                                 (beside --check's budgets when given)
//   omui-bench --report-zh report.md              the same in Chinese (docs/zh)
//   omui-bench --note TEXT                        a note in the report (repeatable; --note-zh for the Chinese)
//   omui-bench --quick                            a few iterations each (a smoke run)
//   omui-bench --list                             names and descriptions
//
// Heap allocations are counted by replacing the global operator new here, in
// the program only: the libraries it measures are the ones every app links.
// (Over-aligned allocations keep the library's own operator new: none of the
// measured paths make them.)
import std;
import openmui.bench;
import mcpplibs.os;

void* operator new(std::size_t size) {
    omui::bench::count_allocation();
    if (void* p = std::malloc(size ? size : 1)) return p;
    std::abort();  // core code uses no exceptions; running out of memory ends the run
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }


namespace {

int usage(std::string_view problem) {
    std::println(std::cerr, "omui-bench: {}\nusage: omui-bench [--filter TEXT] [--json FILE] [--check BUDGETS] [--all-budgets] "
                            "[--report FILE.md] [--report-zh FILE.md] [--note TEXT] [--note-zh TEXT] [--quick] [--min-time MS] [--label TEXT] [--list]",
                 problem);
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    namespace bench = omui::bench;
    bench::add_framework_benchmarks();
    bench::Options options;
    std::optional<std::filesystem::path> json_file, budgets_file, report_file, report_zh;
    bool list = false, require_all = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&]() -> std::optional<std::string_view> {
            if (i + 1 >= argc) return std::nullopt;
            return std::string_view(argv[++i]);
        };
        if (a == "--filter") {
            auto v = value();
            if (!v) return usage("--filter takes a text");
            options.filter = *v;
        } else if (a == "--json") {
            auto v = value();
            if (!v) return usage("--json takes a file");
            json_file = std::filesystem::path(*v);
        } else if (a == "--report") {
            auto v = value();
            if (!v) return usage("--report takes a file");
            report_file = std::filesystem::path(*v);
        } else if (a == "--note") {
            auto v = value();
            if (!v) return usage("--note takes a text");
            options.notes.emplace_back(*v);
        } else if (a == "--note-zh") {
            auto v = value();
            if (!v) return usage("--note-zh takes a text");
            options.notes_zh.emplace_back(*v);
        } else if (a == "--report-zh") {
            auto v = value();
            if (!v) return usage("--report-zh takes a file");
            report_zh = std::filesystem::path(*v);
        } else if (a == "--check") {
            auto v = value();
            if (!v) return usage("--check takes a budgets file");
            budgets_file = std::filesystem::path(*v);
        } else if (a == "--min-time") {
            auto v = value();
            if (!v || std::from_chars(v->data(), v->data() + v->size(), options.min_time_ms).ec != std::errc{})
                return usage("--min-time takes milliseconds");
        } else if (a == "--label") {
            auto v = value();
            if (!v) return usage("--label takes a text");
            options.label = *v;
        } else if (a == "--quick") {
            options.warmup = 1;
            options.min_iterations = 3;
            options.min_time_ms = 0;
        } else if (a == "--all-budgets") {
            require_all = true;
        } else if (a == "--list") {
            list = true;
        } else {
            return usage(std::format("no option `{}`", a));
        }
    }
    if (list) {
        for (const bench::Case& c : bench::cases()) std::println("{:<26} {}", c.name, c.description);
        return 0;
    }
    std::vector<bench::Budget> budgets;
    if (budgets_file) {
        auto text = mcpplibs::os::fs::read(*budgets_file);
        if (!text) return usage(std::format("{}: {}", budgets_file->string(), text.error().message()));
        auto parsed = bench::parse_budgets(*text);
        if (!parsed) return usage(std::format("{}: {}", budgets_file->string(), parsed.error()));
        budgets = std::move(*parsed);
    }
    const bench::Report report = bench::run(options);
    std::print("{}", report.table());
    if (json_file) {
        if (auto w = mcpplibs::os::fs::write_atomic(*json_file, report.json()); !w) {
            std::println(std::cerr, "omui-bench: {}: {}", json_file->string(), w.error().message());
            return 1;
        }
    }
    for (const auto& [file, language] : {std::pair{report_file, "en"}, std::pair{report_zh, "zh"}}) {
        if (!file) continue;
        if (auto w = mcpplibs::os::fs::write_atomic(*file, report.markdown(budgets, language)); !w) {
            std::println(std::cerr, "omui-bench: {}: {}", file->string(), w.error().message());
            return 1;
        }
    }
    if (!budgets_file) return 0;
    const auto violations = report.check(budgets, require_all);
    for (const bench::Violation& v : violations) {
        if (std::isnan(v.value))
            std::println(std::cerr, "over budget: {} has no {} (budget {}: {})", v.budget.name, v.budget.metric, v.budget.max, v.budget.why);
        else
            std::println(std::cerr, "over budget: {} {} = {:.3f}, the limit is {} — {}", v.budget.name, v.budget.metric, v.value, v.budget.max,
                         v.budget.why);
    }
    std::println("budgets: {} checked, {} over", budgets.size(), violations.size());
    return violations.empty() ? 0 : 1;
}
