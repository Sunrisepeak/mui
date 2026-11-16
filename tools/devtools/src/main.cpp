// omui-devtools: the contributors' tool (release design §6.6).
//   ci    a CI run read row by row — each failing member, program and case
//         with its first lines — and set against STATUS's red list (R12).
//         Exit 0 when every failure is named there, 1 when one is new, 2
//         when it could not read the run.
//   platforms  docs/{en,zh}/platforms.md from docs/platforms.omui.json (--check: whether they are current)
//   llvm  what a change touches and its direct consumers, compiled (their
//         tests too, not run) with CI's LLVM and profile: the errors only
//         clang and libc++ give (reds 90, 91, 93), before a push.
//   govern  a command held within a share of the machine: its compilers and
//         linkers at most --jobs at once (tools/dev/capped.sh, AGENTS §5).
//   incremental  the same build twice: the second leaves every compilation
//         output untouched (CI's row; exit 1 when one changed).
//   api   a package's exported surface (api.omui.txt beside it): printed, written, or checked.
//   atspi  examples 03 and 07 read and operated through AT-SPI, as a screen
//         reader does (tools/ci/atspi.sh; exit 1 when a check fails).
//   lead  the lead ledger (D-135): a benchmark's samples, the metrics' verdicts, a capability against
//         competitors, the pages (docs/{en,zh}/lead.md, the evaluation summary) and its check.
//   bench competitors  the comparisons with the best of each field built, installed and run: the README's
//         tables, or (--ledger) rounds alternating, judged into the lead ledger and its pages.
//   gpu   a device for the GPU tests: CI's software one (lavapipe; Metal on macOS) or, with --real, this
//         machine's NVIDIA GPU; its environment written to $GITHUB_ENV, or a command run with it.
//   lint  the repository's rules (D-206: the lint in C++, 43 rules);
//         --self-test shows each rule catches a violation planted in a tree of its own.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.api;
import omui.devtools.atspi;
import omui.devtools.bench;
import omui.devtools.ci;
import omui.devtools.govern;
import omui.devtools.gpu;
import omui.devtools.incremental;
import omui.devtools.lead;
import omui.devtools.lint;
import omui.devtools.members;
import omui.devtools.platforms;

namespace cmd = mcpplibs::cmdline;
namespace rec = omui::devtools::ci;
namespace ws = omui::devtools::members;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// `gh ARGS…`'s standard output, or why not.
std::expected<std::string, std::string> gh(std::vector<std::string> args) {
    const auto program = mcpplibs::os::env::find_executable("gh");
    if (!program) return std::unexpected("`gh` is not on PATH (GitHub's command line reads the run)");
    mcpplibs::os::process::spawn_options options;
    options.program = *program;
    options.args = std::move(args);
    auto ran = mcpplibs::os::process::run(std::move(options));
    if (!ran) return std::unexpected(std::format("gh did not start: {}", ran.error().message()));
    if (ran->status.shell_code() != 0) return std::unexpected(std::format("gh: {}", ran->err));
    return std::move(ran->out);
}

struct Reported {
    rec::Member member;
    std::string package;
    std::vector<rec::Known> known;  // per failure
};

struct Report {
    std::string run;
    std::vector<std::pair<rec::Row, std::vector<Reported>>> rows;
    int fresh = 0;  // failures no red row names
};

Report reconcile(std::string run, std::vector<rec::Row> rows, std::span<const rec::Red> reds, const fs::path& root) {
    Report report{std::move(run)};
    for (auto& row : rows) {
        std::vector<Reported> members;
        for (auto& member : row.members) {
            Reported one{member};
            if (auto manifest = read_file(root / member.path / "mcpp.toml")) one.package = rec::package_of(*manifest);
            for (const auto& failure : member.failures) {
                one.known.push_back(rec::known_in(rec::names_of(member, failure, one.package), reds));
                if (one.known.back().rows.empty()) ++report.fresh;
            }
            members.push_back(std::move(one));
        }
        report.rows.emplace_back(std::move(row), std::move(members));
    }
    return report;
}

void print_text(const Report& report, std::ostream& out) {
    std::size_t failed = 0;
    for (const auto& [row, members] : report.rows) failed += row.conclusion == "failure" || !members.empty() || !row.build_errors.empty();
    out << std::format("run {}: {} rows, {} with failures, {} failure(s) not in the red list\n", report.run, report.rows.size(), failed,
                       report.fresh);
    for (const auto& [row, members] : report.rows) {
        const bool clean = members.empty() && row.build_errors.empty();
        if (clean && row.conclusion != "failure") {
            out << std::format("  {:9} {}\n", row.conclusion.empty() ? "ok" : row.conclusion, row.name);
            continue;
        }
        out << std::format("\n✗ {}{}\n", row.name, row.summary.empty() ? "" : " — " + row.summary);
        for (const auto& error : row.build_errors) out << "    " << error << '\n';
        for (const auto& one : members) {
            out << std::format("  {}{}\n", one.member.path, one.package.empty() ? "" : " (" + one.package + ")");
            for (std::size_t at = 0; at < one.member.failures.size(); ++at) {
                const auto& failure = one.member.failures[at];
                out << std::format("    {}: {}\n", failure.program.empty() ? "package" : failure.program, failure.why);
                if (!failure.last_passed.empty() && failure.cases.empty())
                    out << std::format("      after: {}\n", failure.last_passed.substr(0, 160));
                for (const auto& name : failure.cases) out << "      FAIL " << name << '\n';
                for (const auto& line : failure.head) out << "        " << line << '\n';
                const auto list = [](const std::vector<int>& numbers) {
                    std::string text;
                    for (int number : numbers) text += std::format("{}{}", text.empty() ? "" : ", ", number);
                    return text;
                };
                const auto& known = one.known[at];
                if (!known.rows.empty()) out << "      known: red " << list(known.rows) << '\n';
                else if (!known.package_rows.empty())
                    out << "      NEW: no red row names it (its package is in red " << list(known.package_rows) << ")\n";
                else out << "      NEW: no red row names it\n";
            }
        }
    }
}

json as_json(const Report& report) {
    json rows = json::array();
    for (const auto& [row, members] : report.rows) {
        json list = json::array();
        for (const auto& one : members) {
            json failures = json::array();
            for (std::size_t at = 0; at < one.member.failures.size(); ++at) {
                const auto& failure = one.member.failures[at];
                failures.push_back({{"program", failure.program},
                                    {"why", failure.why},
                                    {"cases", failure.cases},
                                    {"head", failure.head},
                                    {"last_passed", failure.last_passed},
                                    {"red", one.known[at].rows},
                                    {"package_red", one.known[at].package_rows}});
            }
            list.push_back({{"path", one.member.path}, {"package", one.package}, {"failures", std::move(failures)}});
        }
        rows.push_back({{"name", row.name},
                        {"conclusion", row.conclusion},
                        {"summary", row.summary},
                        {"build_errors", row.build_errors},
                        {"members", std::move(list)}});
    }
    return {{"run", report.run}, {"fresh", report.fresh}, {"rows", std::move(rows)}};
}

cmd::App app() {
    using cmd::Arg, cmd::Option;
    return cmd::App("omui-devtools")
        .description("The contributors' tool: reconcile CI with the red list; compile a change with CI's LLVM")
        .option(Option("root").takes_value().value_name("DIR").help("the workspace (default: here)"))
        .subcommand(cmd::App("ci")
                        .description("A CI run read row by row and set against STATUS's red list (exit 1 when a failure is new)")
                        .option(Option("status").takes_value().value_name("FILE").help("the red list (default: .agents/docs/STATUS.md)"))
                        .option(Option("json").help("the report as JSON"))
                        .subcommand(cmd::App("run")
                                        .description("Read a run from GitHub (gh run view: its jobs and its failed jobs' logs)")
                                        .arg(Arg("id").required().help("the run's number")))
                        .subcommand(cmd::App("log")
                                        .description("Read a saved log (gh run view RUN --log-failed > FILE)")
                                        .arg(Arg("file").required().help("the log"))
                                        .option(Option("jobs").takes_value().value_name("FILE").help("the run's jobs (gh run view RUN --json jobs)"))))
        .subcommand(cmd::App("govern")
                        .description("Run a command with at most --jobs of its compilers and linkers at once, fewer while memory is short (AGENTS §5)")
                        .option(Option("jobs").takes_value().value_name("N").help("how many may run at once"))
                        .option(Option("memory-floor").takes_value().value_name("SHARE").help("under this share of the memory available, fewer (default: 0.10)"))
                        .option(Option("interval").takes_value().value_name("MS").help("how often it looks (default: 250)"))
                        .arg(Arg("command").required().help("after --: the command and its arguments")))
        .subcommand(cmd::App("incremental")
                        .description("Build twice: the second, unchanged, must leave every compilation output untouched (exit 1 when one changed)")
                        .option(Option("package").short_name('p').takes_value().multiple().value_name("MEMBER").help("these members"))
                        .option(Option("workspace").help("the whole workspace"))
                        .option(Option("exclude").takes_value().multiple().value_name("MEMBER").help("not this one (with --workspace)"))
                        .option(Option("profile").takes_value().value_name("P").help("default: dev"))
                        .option(Option("toolchain").takes_value().value_name("T").help("default: mcpp's"))
                        .option(Option("locked").help("mcpp's --locked"))
                        .option(Option("report").takes_value().value_name("FILE").help("default: target/incremental.report.omui.json")))
        .subcommand(cmd::App("api")
                        .description("A package's exported surface, as its interface units declare it: printed, written to api.omui.txt, or checked against it")
                        .option(Option("package").short_name('p').takes_value().multiple().value_name("MEMBER").help("the members (paths as the workspace lists them)"))
                        .option(Option("write").help("write each member's api.omui.txt"))
                        .option(Option("check").help("exit 1 where a member's api.omui.txt is not its surface now")))
        .subcommand(cmd::App("atspi")
                        .description("Examples 03 and 07 read and operated through AT-SPI as a screen reader does; a JSON report (exit 1 when a check fails)")
                        .arg(Arg("tally").required().help("example 03's program (tally)"))
                        .arg(Arg("notes").help("example 07's program (notes-agent)")))
        .subcommand(cmd::App("platforms")
                        .description("Write docs/{en,zh}/platforms.md from docs/platforms.omui.json (the verification table, D-201)")
                        .option(Option("check").help("only say whether the pages are current (exit 1 when not)")))
        .subcommand(cmd::App("llvm")
                        .description("Compile what a change touches and its direct consumers, tests included, with CI's LLVM and profile (not run)")
                        .option(Option("since").takes_value().value_name("REF").help("the change: files differing from REF, and new ones (default: @{upstream})"))
                        .option(Option("package").short_name('p').takes_value().multiple().value_name("MEMBER").help("these members instead of the change's"))
                        .option(Option("toolchain").takes_value().value_name("T").help("default: the LLVM of CI's rows (.github/workflows/ci.yml)"))
                        .option(Option("profile").takes_value().value_name("P").help("default: ci"))
                        .option(Option("no-consumers").help("only the members themselves"))
                        .option(Option("dry").help("say what would be compiled, and how")))
        .subcommand(cmd::App("lint")
                        .description("The repository's rules, checked (exit 1 when a rule is broken)")
                        .option(Option("rule").takes_value().multiple().value_name("NAME").help("only these rules"))
                        .option(Option("list").help("the rules and what each checks"))
                        .option(Option("self-test").help("each rule's violation planted in a tree of its own: caught, and by it alone")))
        .subcommand(cmd::App("gpu")
                        .description("A GPU for the device tests: the variables written to $GITHUB_ENV (or printed), or a command run with them")
                        .option(Option("real").help("this machine's NVIDIA GPU (Linux), not CI's software device"))
                        .arg(Arg("command").help("after --: the command to run on the device")))
        .subcommand(cmd::App("bench")
                        .description("The benchmarks against the best of each field (tests/bench/competitors)")
                        .subcommand(cmd::App("competitors")
                                        .description("Build our side, install the pinned competitors, run every comparison")
                                        .option(Option("ledger").help("the lead ledger: rounds alternating, each metric judged, the ledger and its pages written"))
                                        .option(Option("rounds").takes_value().value_name("N").help("with --ledger: how many (default: 15)"))
                                        .option(Option("dry-run").help("with --ledger: print the verdicts, write nothing"))))
        .subcommand(cmd::App("lead")
                        .description("The lead ledger (D-135): samples, verdicts, capabilities, its pages and its check")
                        .subcommand(cmd::App("sample")
                                        .description("Run a benchmark; tag each JSON line it prints with the series, the round and the key; append the samples")
                                        .option(Option("series").takes_value().value_name("NAME").help("the series (when the line names none)"))
                                        .option(Option("series-field").takes_value().value_name("F").help("each line's series from this field"))
                                        .option(Option("round").takes_value().value_name("K").help("the round"))
                                        .option(Option("key").takes_value().value_name("KEY").help("the key, where the line has no scene"))
                                        .option(Option("out").takes_value().value_name("FILE").help("the samples file"))
                                        .arg(Arg("command").required().help("after --: the benchmark and its arguments")))
                        .subcommand(cmd::App("verdict")
                                        .description("Judge every measured metric by the samples; append one ledger line each")
                                        .arg(Arg("samples").required().help("the samples file"))
                                        .option(Option("dry-run").help("print the verdicts, write nothing")))
                        .subcommand(cmd::App("capability")
                                        .description("Record a capability against competitors (none | partial | app-defined | full)")
                                        .option(Option("project").takes_value().value_name("P"))
                                        .option(Option("feature").takes_value().value_name("F"))
                                        .option(Option("about").takes_value().value_name("TEXT"))
                                        .option(Option("test").takes_value().value_name("PATH").help("the test that shows it"))
                                        .option(Option("competitor").takes_value().multiple().value_name("NAME=SUPPORT")))
                        .subcommand(cmd::App("docs").description("Write docs/{en,zh}/lead.md and the evaluation summary from the ledger"))
                        .subcommand(cmd::App("check").description("The signature features against the ledger (lint `lead`; exit 1 when one is wrong)")));
}

void conclude(std::vector<rec::Row>& rows, const json& jobs) {
    if (!jobs.contains("jobs")) return;
    for (const auto& job : jobs["jobs"]) {
        const std::string name = job.value("name", "");
        const std::string conclusion = job.contains("conclusion") && job["conclusion"].is_string() ? job["conclusion"].get<std::string>()
                                       : job.value("status", "");
        auto found = std::ranges::find(rows, name, &rec::Row::name);
        if (found == rows.end()) rows.push_back(rec::Row{name, conclusion});
        else found->conclusion = conclusion;
    }
}

int ci(const cmd::ParsedArgs& options, const fs::path& root) {
    if (!options.has_subcommand()) {
        std::cerr << "usage: omui-devtools ci [--json] run ID | log FILE [--jobs FILE]\n";
        return 2;
    }
    const std::string status_name = options.value("status").value_or(".agents/docs/STATUS.md");
    const auto status = read_file(options.value("status") ? mcpplibs::os::fs::from_utf8(status_name) : root / ".agents/docs/STATUS.md");
    if (!status) {
        std::cerr << std::format("omui-devtools ci: cannot read the red list at {} (--status names it)\n", status_name);
        return 2;
    }
    const auto reds = rec::read_red_list(*status);
    const cmd::ParsedArgs& sub = options.subcommand()->get();
    std::string run, log;
    json jobs;
    if (options.subcommand_name() == "run") {
        run = sub.positional(0);
        auto listed = gh({"run", "view", run, "--json", "jobs"});
        if (!listed) {
            std::cerr << "omui-devtools ci: " << listed.error() << '\n';
            return 2;
        }
        jobs = json::parse(*listed, nullptr, false);
        auto failed = gh({"run", "view", run, "--log-failed"});
        if (!failed) {
            std::cerr << "omui-devtools ci: " << failed.error() << '\n';
            return 2;
        }
        log = std::move(*failed);
    } else {
        run = sub.positional(0);
        auto read = read_file(mcpplibs::os::fs::from_utf8(run));
        if (!read) {
            std::cerr << std::format("omui-devtools ci: cannot read {}\n", run);
            return 2;
        }
        log = std::move(*read);
        if (auto file = sub.value("jobs")) {
            auto text = read_file(mcpplibs::os::fs::from_utf8(*file));
            if (!text) {
                std::cerr << std::format("omui-devtools ci: cannot read {}\n", *file);
                return 2;
            }
            jobs = json::parse(*text, nullptr, false);
        }
    }
    auto rows = rec::read_log(log);
    if (jobs.is_object()) conclude(rows, jobs);
    const auto report = reconcile(run, std::move(rows), reds, root);
    if (options.is_flag_set("json")) std::cout << as_json(report).dump(2) << '\n';
    else print_text(report, std::cout);
    return report.fresh > 0 ? 1 : 0;
}

// `git ARGS…` in the workspace: its output's lines.
std::expected<std::vector<std::string>, std::string> git_lines(const fs::path& root, std::vector<std::string> args) {
    const auto program = mcpplibs::os::env::find_executable("git");
    if (!program) return std::unexpected("`git` is not on PATH");
    mcpplibs::os::process::spawn_options options;
    options.program = *program;
    options.args = {"-C", mcpplibs::os::fs::utf8(root)};
    options.args.insert(options.args.end(), args.begin(), args.end());
    auto ran = mcpplibs::os::process::run(std::move(options));
    if (!ran) return std::unexpected(std::format("git did not start: {}", ran.error().message()));
    if (ran->status.shell_code() != 0) return std::unexpected(std::format("git {}: {}", args.front(), ran->err));
    std::vector<std::string> lines;
    for (const auto part : std::views::split(std::string_view(ran->out), '\n'))
        if (!std::string_view(part.begin(), part.end()).empty()) lines.emplace_back(part.begin(), part.end());
    return lines;
}

// The LLVM CI's rows build with, as .github/workflows/ci.yml names it ("llvm@23.1.3").
std::string ci_llvm(const fs::path& root) {
    const auto text = read_file(root / ".github/workflows/ci.yml");
    if (!text) return {};
    const auto at = text->find("llvm@");
    if (at == std::string::npos) return {};
    auto end = at + 5;
    while (end < text->size() && (std::isdigit(static_cast<unsigned char>((*text)[end])) || (*text)[end] == '.')) ++end;
    return text->substr(at, end - at);
}

int llvm(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto manifest = read_file(root / "mcpp.toml");
    if (!manifest) {
        std::cerr << "omui-devtools llvm: no mcpp.toml in the workspace (--root names it)\n";
        return 2;
    }
    const auto paths = ws::listed(*manifest);
    std::vector<ws::Member> all;
    for (const auto& path : paths) {
        const auto own = read_file(root / mcpplibs::os::fs::from_utf8(path) / "mcpp.toml");
        all.push_back({path, own ? ws::uses_of(path, *own, paths) : std::vector<std::string>{}});
    }
    std::vector<std::string> chosen = options.option_or_empty("package").values;
    for (auto& path : chosen)
        while (path.ends_with('/')) path.pop_back();
    for (const auto& path : chosen)
        if (!std::ranges::contains(paths, path)) {
            std::cerr << std::format("omui-devtools llvm: `{}` is not a workspace member (a path as mcpp.toml lists it)\n", path);
            return 2;
        }
    if (chosen.empty()) {
        const std::string since = options.value("since").value_or("@{upstream}");
        auto changed = git_lines(root, {"diff", "--name-only", since});
        if (!changed) {
            std::cerr << "omui-devtools llvm: " << changed.error() << " (--since REF names the change's base)\n";
            return 2;
        }
        auto fresh = git_lines(root, {"ls-files", "--others", "--exclude-standard"});
        if (fresh) changed->insert(changed->end(), fresh->begin(), fresh->end());
        for (const auto& file : *changed) {
            const auto owner = ws::owner_of(file, all);
            if (!owner.empty() && !std::ranges::contains(chosen, owner)) chosen.push_back(owner);
        }
        if (chosen.empty()) {
            std::cout << std::format("nothing to compile: no member's files differ from {}\n", since);
            return 0;
        }
    }
    const auto picked = options.is_flag_set("no-consumers") ? chosen : ws::with_consumers(chosen, all);
    const std::string toolchain = options.value("toolchain").value_or(ci_llvm(root));
    if (toolchain.empty()) {
        std::cerr << "omui-devtools llvm: CI's LLVM not found in .github/workflows/ci.yml (--toolchain names one)\n";
        return 2;
    }
    std::vector<std::string> args{"test", "--toolchain", toolchain, "--profile", options.value("profile").value_or("ci"), "--no-run"};
    for (const auto& path : picked) args.insert(args.end(), {"-p", path});
    // Within 90% of the machine (AGENTS §5): through tools/dev/capped.sh where it is.
    const fs::path capped = root / "tools/dev/capped.sh";
    std::error_code missing;
    const bool through_capped = !mcpplibs::os::is_windows && fs::exists(capped, missing);
    std::cout << std::format("{} member(s) changed, {} with their direct consumers; {}\n", chosen.size(), picked.size(), toolchain);
    for (const auto& path : picked) std::cout << "  " << path << (std::ranges::contains(chosen, path) ? "" : "  (consumer)") << '\n';
    std::string shown = through_capped ? "tools/dev/capped.sh mcpp" : "mcpp";
    for (const auto& arg : args) shown += " " + arg;
    std::cout << shown << '\n';
    if (options.is_flag_set("dry")) return 0;
    const auto mcpp = mcpplibs::os::env::find_executable("mcpp");
    if (!mcpp) {
        std::cerr << "omui-devtools llvm: `mcpp` is not on PATH\n";
        return 2;
    }
    mcpplibs::os::process::spawn_options spawn;
    if (through_capped) {
        spawn.program = capped;
        spawn.args = {"mcpp"};
        spawn.args.insert(spawn.args.end(), args.begin(), args.end());
    } else {
        spawn.program = *mcpp;
        spawn.args = std::move(args);
    }
    spawn.cwd = root;
    spawn.out = mcpplibs::os::process::stdio::inherit;
    spawn.err = mcpplibs::os::process::stdio::inherit;
    auto process = mcpplibs::os::process::process::spawn(spawn);
    if (!process) {
        std::cerr << std::format("omui-devtools llvm: mcpp did not start: {}\n", process.error().message());
        return 2;
    }
    auto status = process->wait();
    return status ? status->shell_code() : 1;
}

int govern(const cmd::ParsedArgs& options) {
    namespace held = omui::devtools::govern;
    held::Options given;
    const auto number = [](std::string_view text, auto& value) {
        const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), value);
        return failed == std::errc{} && end == text.data() + text.size();
    };
    if (!number(options.value("jobs").value_or(""), given.jobs) || given.jobs < 1) {
        std::cerr << "omui-devtools govern: --jobs takes a count of at least 1\n";
        return 2;
    }
    if (const auto floor = options.value("memory-floor"); floor && (!number(*floor, given.memory_floor) || given.memory_floor < 0 || given.memory_floor >= 1)) {
        std::cerr << "omui-devtools govern: --memory-floor takes a share in [0, 1)\n";
        return 2;
    }
    if (const auto every = options.value("interval")) {
        std::int64_t milliseconds = 0;
        if (!number(*every, milliseconds) || milliseconds < 10) {
            std::cerr << "omui-devtools govern: --interval takes milliseconds, at least 10\n";
            return 2;
        }
        given.interval = std::chrono::milliseconds(milliseconds);
    }
    for (std::size_t index = 0; index < options.positional_count(); ++index) given.command.push_back(options.positional(index));
    const auto status = held::run(given);
    if (!status) {
        std::cerr << "omui-devtools govern: " << status.error() << '\n';
        return 127;
    }
    return *status;
}

int incremental(const cmd::ParsedArgs& options) {
    namespace check = omui::devtools::incremental;
    check::Options given;
    given.packages = options.option_or_empty("package").values;
    given.workspace = options.is_flag_set("workspace");
    given.excluded = options.option_or_empty("exclude").values;
    if (given.workspace == !given.packages.empty()) {
        std::cerr << "omui-devtools incremental: either --workspace or -p MEMBER …\n";
        return 2;
    }
    if (!given.excluded.empty() && !given.workspace) {
        std::cerr << "omui-devtools incremental: --exclude goes with --workspace\n";
        return 2;
    }
    given.profile = options.value("profile").value_or("dev");
    given.toolchain = options.value("toolchain").value_or("");
    given.locked = options.is_flag_set("locked");
    if (const auto report = options.value("report")) given.report = mcpplibs::os::fs::from_utf8(*report);
    const auto mcpp = mcpplibs::os::env::get("MCPP");
    if (mcpp && !mcpp->empty()) given.mcpp = mcpplibs::os::fs::from_utf8(*mcpp);
    else if (const auto found = mcpplibs::os::env::find_executable("mcpp")) given.mcpp = *found;
    else {
        std::cerr << "omui-devtools incremental: `mcpp` is not on PATH (MCPP names it)\n";
        return 2;
    }
    return check::check(given, std::cout, std::cerr) ? 0 : 1;
}

int surface(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto members = options.option_or_empty("package").values;
    if (members.empty()) {
        std::cerr << "omui-devtools api: -p MEMBER … (a path as the workspace lists it)\n";
        return 2;
    }
    int differs = 0;
    for (const std::string& member : members) {
        const fs::path directory = root / mcpplibs::os::fs::from_utf8(member);
        const auto manifest = read_file(directory / "mcpp.toml");
        if (!manifest) {
            std::cerr << std::format("omui-devtools api: no mcpp.toml in {}\n", member);
            return 2;
        }
        const std::string now = omui::devtools::api::surface(directory, omui::devtools::api::interface_globs(*manifest));
        const fs::path file = directory / "api.omui.txt";
        if (options.is_flag_set("check")) {
            if (read_file(file) != now) {
                std::cerr << std::format("{}: api.omui.txt is not its surface now (omui-devtools api -p {} --write)\n", member, member);
                ++differs;
            }
        } else if (options.is_flag_set("write")) {
            std::ofstream(file, std::ios::binary) << now;
            std::cout << std::format("wrote {}/api.omui.txt ({} lines)\n", member, std::ranges::count(now, '\n'));
        } else {
            std::cout << now;
        }
    }
    return differs ? 1 : 0;
}

int lint(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace lint = omui::devtools::lint;
    if (options.is_flag_set("list")) {
        for (const auto& rule : lint::rules()) std::cout << std::format("  {:14} {}\n", rule.name, rule.summary);
        return 0;
    }
    if (options.is_flag_set("self-test")) {
        std::error_code error;
        const fs::path scratch = fs::temp_directory_path(error) / std::format("omui-devtools-lint-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        const int failed = lint::self_test(root, scratch, std::cout);
        fs::remove_all(scratch, error);
        return failed ? 1 : 0;
    }
    const auto problems = lint::run(root, options.option_or_empty("rule").values);
    for (const auto& problem : problems) std::cout << problem.text() << '\n';
    std::cout << std::format("lint: {} problem(s)\n", problems.size());
    return problems.empty() ? 0 : 1;
}

int lead_command(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace lead = omui::devtools::lead;
    if (options.subcommand_name() == "check") {
        const auto found = lead::problems(root);
        for (const auto& [where, what] : found) std::cout << std::format("[lead] {}: {}\n", where, what);
        return found.empty() ? 0 : 1;
    }
    const cmd::ParsedArgs& sub = options.has_subcommand() ? options.subcommand()->get() : options;
    if (options.subcommand_name() == "sample") {
        int round = 0;
        const std::string round_text = sub.value("round").value_or("");
        const auto [end, failed] = std::from_chars(round_text.data(), round_text.data() + round_text.size(), round);
        if (failed != std::errc{} || end != round_text.data() + round_text.size() || !sub.value("series") || !sub.value("out")) {
            std::cerr << "omui-devtools lead sample: --series NAME --round K --out FILE -- COMMAND…\n";
            return 2;
        }
        mcpplibs::os::process::spawn_options run;
        run.program = sub.positional(0);
        for (std::size_t index = 1; index < sub.positional_count(); ++index) run.args.push_back(sub.positional(index));
        auto done = mcpplibs::os::process::run(std::move(run));
        if (!done) {
            std::cerr << std::format("omui-devtools lead sample: {} did not start: {}\n", sub.positional(0), done.error().message());
            return 127;
        }
        if (done->status.shell_code() != 0) {
            std::cerr << done->err;
            return done->status.shell_code();
        }
        const std::string series = *sub.value("series");
        const auto records = lead::samples(done->out, series, sub.value("series-field").value_or(""), sub.value("key").value_or(""), round);
        if (records.empty()) {
            std::cerr << std::format("omui-devtools lead sample: {} printed no JSON line\n", series);
            return 1;
        }
        return lead::append(fs::path(*sub.value("out")), records) ? 0 : 2;
    }
    const fs::path ledger = root / lead::ledger_path;
    if (options.subcommand_name() == "verdict") {
        std::vector<std::string> skipped;
        const auto judged = lead::verdicts(read_file(root / lead::metrics_path).value_or(""), read_file(fs::path(sub.positional(0))).value_or(""),
                                           read_file(ledger).value_or(""), lead::context_here(root), skipped);
        if (!judged) {
            std::cerr << "omui-devtools lead verdict: " << judged.error() << '\n';
            return 2;
        }
        for (const auto& id : skipped) std::cerr << std::format("omui-devtools lead verdict: {}: no paired samples\n", id);
        std::vector<std::string> records;
        for (const auto& verdict : *judged) {
            std::cout << verdict.said << '\n';
            records.push_back(verdict.record);
        }
        return sub.is_flag_set("dry-run") || lead::append(ledger, records) ? 0 : 2;
    }
    if (options.subcommand_name() == "capability") {
        std::vector<std::string> names, supports;
        for (const auto& item : sub.option_or_empty("competitor").values) {
            const auto equals = item.find('=');
            const std::string support = equals == std::string::npos ? std::string{} : item.substr(equals + 1);
            if (!std::ranges::contains(lead::supports, std::string_view(support))) {
                std::cerr << std::format("omui-devtools lead capability: {}: support is one of none, partial, app-defined, full\n", item);
                return 2;
            }
            names.push_back(item.substr(0, equals));
            supports.push_back(support);
        }
        const std::string test = sub.value("test").value_or("");
        if (names.empty() || !sub.value("project") || !sub.value("feature") || !sub.value("about") || test.empty()) {
            std::cerr << "omui-devtools lead capability: --project P --feature F --about TEXT --test PATH --competitor NAME=SUPPORT…\n";
            return 2;
        }
        if (!fs::exists(root / test)) {
            std::cerr << std::format("omui-devtools lead capability: {} does not exist\n", test);
            return 2;
        }
        auto competitors = nlohmann::ordered_json::array();
        for (std::size_t at = 0; at < names.size(); ++at) competitors.push_back({{"name", names[at]}, {"support", supports[at]}});
        const auto made = lead::context_here(root);
        const nlohmann::ordered_json record{{"kind", "capability"},     {"date", made.date},         {"project", *sub.value("project")},
                                            {"feature", *sub.value("feature")}, {"about", *sub.value("about")}, {"test", test},
                                            {"competitors", competitors}, {"verdict", lead::capability_verdict(supports)}, {"commit", made.commit}};
        std::cout << record.dump() << '\n';
        return lead::append(ledger, std::vector{record.dump()}) ? 0 : 2;
    }
    if (options.subcommand_name() != "docs") {
        std::cerr << "usage: omui-devtools lead sample … | verdict SAMPLES | capability … | docs | check\n";
        return 2;
    }
    const auto written = lead::write_pages(root);
    if (!written) {
        std::cerr << "omui-devtools lead docs: " << written.error() << '\n';
        return 2;
    }
    for (const auto& path : *written) std::cout << std::format("wrote {}\n", path);
    return 0;
}

int platforms(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto table = read_file(root / "docs/platforms.omui.json");
    if (!table) {
        std::cerr << "omui-devtools platforms: cannot read docs/platforms.omui.json\n";
        return 2;
    }
    int stale = 0;
    for (const std::string_view language : {"en", "zh"}) {
        const auto written = omui::devtools::platforms::page(*table, language);
        if (!written) {
            std::cerr << "omui-devtools platforms: " << written.error() << '\n';
            return 2;
        }
        const fs::path target = root / "docs" / std::string(language) / "platforms.md";
        if (read_file(target) == *written) continue;
        if (options.is_flag_set("check")) {
            std::cerr << std::format("omui-devtools platforms: docs/{}/platforms.md is not current (omui-devtools platforms)\n", language);
            ++stale;
            continue;
        }
        std::ofstream(target, std::ios::binary) << *written;
        std::cout << std::format("wrote docs/{}/platforms.md\n", language);
    }
    return stale ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    auto application = app();
    cmd::Argv args(argv, argv + argc);
    auto parsed = application.parse_from(args);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::cerr << "omui-devtools: " << parsed.error().message << '\n';
        return 2;
    }
    const fs::path root = mcpplibs::os::fs::from_utf8(parsed->value("root").value_or("."));
    if (parsed->subcommand_name() == "ci") return ci(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "llvm") return llvm(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "platforms") return platforms(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "api") return surface(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "lint") return lint(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "gpu") {
        namespace device = omui::devtools::gpu;
        const auto& given = parsed->subcommand()->get();
        const auto prepared = device::prepare(given.is_flag_set("real") ? device::Device::real : device::Device::software);
        if (!prepared) {
            std::cerr << "omui-devtools gpu: " << prepared.error() << '\n';
            return 1;
        }
        for (const auto& line : prepared->said) std::cout << "gpu: " << line << '\n';
        if (given.positional_count() == 0) {
            const auto github = mcpplibs::os::env::get("GITHUB_ENV");
            std::ofstream file;
            if (github) file.open(*github, std::ios::binary | std::ios::app);
            for (const auto& [name, value] : prepared->variables) {
                std::cout << std::format("gpu: {}={}\n", name, value);
                if (file.is_open()) file << name << '=' << value << '\n';
            }
            return github && !file ? 2 : 0;
        }
        mcpplibs::os::process::spawn_options run;
        run.program = given.positional(0);
        for (std::size_t index = 1; index < given.positional_count(); ++index) run.args.push_back(given.positional(index));
        run.out = run.err = mcpplibs::os::process::stdio::inherit;  // it becomes this program
        for (const auto& [name, value] : prepared->variables) run.env.emplace_back(name, value);
        // A command run here on the software device makes no window either (CI's rows set this themselves).
        if (!mcpplibs::os::is_macos && !mcpplibs::os::env::get("SDL_VIDEO_DRIVER")) run.env.emplace_back("SDL_VIDEO_DRIVER", "offscreen");
        const auto failed = mcpplibs::os::process::exec(std::move(run));
        std::cerr << std::format("omui-devtools gpu: {} did not start: {}\n", given.positional(0), failed ? std::string("returned") : failed.error().message());
        return 127;
    }
    if (parsed->subcommand_name() == "bench") {
        const auto& sub = parsed->subcommand()->get();
        if (sub.subcommand_name() != "competitors") {
            std::cerr << "usage: omui-devtools bench competitors [--ledger [--rounds N] [--dry-run]]\n";
            return 2;
        }
        const auto& given = sub.subcommand()->get();
        omui::devtools::bench::Options options;
        if (given.is_flag_set("ledger")) {
            options.rounds = 15;
            if (const auto rounds = given.value("rounds")) {
                const auto [end, failed] = std::from_chars(rounds->data(), rounds->data() + rounds->size(), options.rounds);
                if (failed != std::errc{} || end != rounds->data() + rounds->size() || options.rounds < 1) {
                    std::cerr << "omui-devtools bench competitors: --rounds takes a count of at least 1\n";
                    return 2;
                }
            }
            options.dry_run = given.is_flag_set("dry-run");
        }
        const auto status = omui::devtools::bench::competitors(root, options);
        if (!status) {
            std::cerr << "omui-devtools bench competitors: " << status.error() << '\n';
            return 2;
        }
        return *status;
    }
    if (parsed->subcommand_name() == "lead") return lead_command(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "atspi") {
        const auto& sub = parsed->subcommand()->get();
        const auto report = omui::devtools::atspi::check(mcpplibs::os::fs::from_utf8(sub.positional(0)),
                                                         sub.positional_count() > 1 ? mcpplibs::os::fs::from_utf8(sub.positional(1)) : fs::path{});
        std::cout << report.json() << '\n';
        return report.ok() ? 0 : 1;
    }
    if (parsed->subcommand_name() == "govern") return govern(parsed->subcommand()->get());
    if (parsed->subcommand_name() == "incremental") return incremental(parsed->subcommand()->get());
    std::cerr << "usage: omui-devtools [--root DIR] ci … | llvm … | platforms | govern … | incremental … | api … | atspi … | lint … | lead … | bench … | gpu …  (omui-devtools --help)\n";
    return 2;
}
