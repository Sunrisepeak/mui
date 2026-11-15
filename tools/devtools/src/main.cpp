// omui-devtools: the contributors' tool (release design §6.6).
//   ci    a CI run read row by row — each failing member, program and case
//         with its first lines — and set against STATUS's red list (R12).
//         Exit 0 when every failure is named there, 1 when one is new, 2
//         when it could not read the run.
//   platforms  docs/{en,zh}/platforms.md from docs/platforms.omui.json (--check: whether they are current)
//   llvm  what a change touches and its direct consumers, compiled (their
//         tests too, not run) with CI's LLVM and profile: the errors only
//         clang and libc++ give (reds 90, 91, 93), before a push.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import nlohmann.json;
import omui.devtools.ci;
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
                        .option(Option("dry").help("say what would be compiled, and how")));
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
    std::cerr << "usage: omui-devtools [--root DIR] ci … | llvm … | platforms  (omui-devtools --help)\n";
    return 2;
}
