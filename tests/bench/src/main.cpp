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
import omui.bench;
import mcpplibs.os;
import mcpplibs.cmdline;

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
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    const auto line = App("omui-bench")
        .description("Runs openmui's benchmarks, writes the report, checks budgets")
        .option(Option("filter").takes_value().value_name("TEXT").help("the ones whose name contains it"))
        .option(Option("json").takes_value().value_name("FILE").help("also the report as JSON"))
        .option(Option("check").takes_value().value_name("BUDGETS").help("exit 1 when a budget is exceeded"))
        .option(Option("report").takes_value().value_name("FILE").help("also the report for people, in Markdown"))
        .option(Option("report-zh").takes_value().value_name("FILE").help("the same in Chinese"))
        .option(Option("note").takes_value().multiple().value_name("TEXT").help("a note in the report"))
        .option(Option("note-zh").takes_value().multiple().value_name("TEXT").help("a note in the Chinese report"))
        .option(Option("min-time").value_type<double>().value_name("MS").help("the least time a benchmark runs"))
        .option(Option("label").takes_value().help("the run's label"))
        .option(Option("quick").help("a few iterations each (a smoke run)"))
        .option(Option("all-budgets").help("every benchmark must have a budget"))
        .option(Option("list").help("names and descriptions"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) return parsed.error().is_error() ? usage(parsed.error().message) : 0;
    const auto& args = *parsed;
    bench::Options options;
    const auto path = [&](std::string_view name) -> std::optional<std::filesystem::path> {
        if (auto value = args.value(name)) return std::filesystem::path(*value);
        return std::nullopt;
    };
    const std::optional<std::filesystem::path> json_file = path("json"), budgets_file = path("check"), report_file = path("report"),
                                               report_zh = path("report-zh");
    const bool list = args.is_flag_set("list"), require_all = args.is_flag_set("all-budgets");
    if (auto filter = args.value("filter")) options.filter = *filter;
    if (auto label = args.value("label")) options.label = *label;
    for (const std::string& note : args.option_or_empty("note").values) options.notes.emplace_back(note);
    for (const std::string& note : args.option_or_empty("note-zh").values) options.notes_zh.emplace_back(note);
    if (args.is_flag_set("quick")) {
        options.warmup = 1;
        options.min_iterations = 3;
        options.min_time_ms = 0;
    }
    if (auto min_time = args.value_as<double>("min-time")) options.min_time_ms = *min_time;
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
    // A benchmark whose measurement went wrong fails the run, budgets or not.
    std::size_t failed = 0;
    for (const bench::Result& r : report.results)
        if (!r.failed.empty()) {
            std::println(std::cerr, "failed: {} — {}", r.name, r.failed);
            ++failed;
        }
    if (!budgets_file) return failed ? 1 : 0;
    const auto violations = report.check(budgets, require_all);
    for (const bench::Violation& v : violations) {
        if (std::isnan(v.value))
            std::println(std::cerr, "over budget: {} has no {} (budget {}: {})", v.budget.name, v.budget.metric, v.budget.max, v.budget.why);
        else
            std::println(std::cerr, "over budget: {} {} = {:.3f}, the limit is {} — {}", v.budget.name, v.budget.metric, v.value, v.budget.max,
                         v.budget.why);
    }
    std::println("budgets: {} checked, {} over", budgets.size(), violations.size());
    return violations.empty() && !failed ? 0 : 1;
}
