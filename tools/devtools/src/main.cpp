// omui-devtools: the contributors' tool (release design §6.6).
//   ci    a CI run read row by row — each failing member, program and case
//         with its first lines — and set against STATUS's red list (R12).
//         Exit 0 when every failure is named there, 1 when one is new, 2
//         when it could not read the run.
//   platforms  docs/{en,zh}/platforms.md from docs/release.platforms.xdef.json (--check: whether they are current)
//   closure    docs/{en,zh}/closure.md: each project's closure checklist read from the tree (--check, --gate)
//   docs  docs/{en,zh}/maturity.md, experimental.md and reference.md from the manifests, the experimental READMEs, the catalogs and the contracts (--check)
//   llvm  what a change touches and its direct consumers, compiled (their
//         tests too, not run) with CI's LLVM and profile: the errors only
//         clang and libc++ give (reds 90, 91, 93), before a push.
//   govern  a command held within a share of the machine: its compilers and
//         linkers at most 90% of the machine at once, low priority (AGENTS §5).
//   incremental  the same build twice: the second leaves every compilation
//         output untouched (CI's row; exit 1 when one changed).
//   api   a package's exported surface (api.omui.txt beside it): printed, written, or checked.
//   atspi  examples 03 and 07 read and operated through AT-SPI, as a screen
//         reader does (check atspi builds them and runs it; exit 1 when a check fails).
//   lead  the lead ledger (D-135): a benchmark's samples, the metrics' verdicts, a capability against
//         competitors, the pages (docs/{en,zh}/lead.md, the evaluation summary) and its check.
//   bench competitors  the comparisons with the best of each field built, installed and run: the README's
//         tables, or (--ledger) rounds alternating, judged into the lead ledger and its pages.
//   gpu   a device for the GPU tests: CI's software one (lavapipe; Metal on macOS) or, with --real, this
//         machine's NVIDIA GPU; its environment written to $GITHUB_ENV, or a command run with it.
//   check CI's acceptance steps, each building what it needs and holding the result: features (M0 ⑤),
//         templates (M0 ①), reflect (V-08's canary, then the suite), sweep (D-91), tools-e2e, agent-e2e (M1 ②),
//         toy (Bilibili Toy), size (a page's WebAssembly bytes, held to a budget), atspi (examples 03 and 07 on a private bus).
//   browser  a wasm test program in a real page of headless Chromium (mlibs.http's framing on the loopback).
//   media the showcases' and the site's README media, made by their tests: copied in, or (--check) held to them.
//   consumer  an out-of-tree consumer probe (hanim-core, hanim-api, hanim-play, omui-hanim, hanim-features):
//         a fresh project as a user's, what mcpp resolved and compiled audited, a report kept.
//   site  the website: built for the Web, pre-rendered, split by its first screen (build), checked for what it
//         carries (verify), split alone (split), its first screen profiled (profile), a page driven (drive).
//   lint  the repository's rules (D-206: the lint in C++, 40 rules);
//         --self-test shows each rule catches a violation planted in a tree of its own.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import mlibs.hash;
import mlibs.image;
import nlohmann.json;
import omui.devtools.api;
import omui.devtools.atspi;
import omui.devtools.bench;
import omui.devtools.browser;
import omui.devtools.check;
import omui.devtools.ci;
import omui.devtools.closure;
import omui.devtools.consumer;
import omui.devtools.docs;
import omui.devtools.eval;
import omui.devtools.govern;
import omui.devtools.gpu;
import omui.devtools.incremental;
import omui.devtools.lanes;
import omui.devtools.lead;
import omui.devtools.lint;
import omui.devtools.members;
import omui.devtools.platforms;
import omui.devtools.run;
import omui.devtools.release;
import omui.devtools.rename;
import omui.devtools.split;
import omui.devtools.site;

namespace cmd = mcpplibs::cmdline;
namespace rec = omui::devtools::ci;
namespace lanes = omui::devtools::lanes;
namespace ws = omui::devtools::members;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

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
            if (auto manifest = mcpplibs::os::fs::read(root / member.path / "mcpp.toml")) one.package = rec::package_of(*manifest);
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
                                        .option(Option("jobs").takes_value().value_name("FILE").help("the run's jobs (gh run view RUN --json jobs)")))
                        .subcommand(cmd::App("local")
                                        .description("CI's matrix on this one Linux machine, for when hosted runners are unavailable (wine for the Windows row)")
                                        .arg(Arg("member").help("these members (default: the workspace)")))
                        .subcommand(cmd::App("plan")
                                        .description("Which of CI's lanes a run tests and which it requests (.github/ci.lanes.xdef.json, D-254); by default, what pushing HEAD to a pull request runs")
                                        .option(Option("event").takes_value().value_name("EVENT").help("pull_request (default), workflow_dispatch, push or schedule"))
                                        .option(Option("lanes").takes_value().value_name("SELECTION").help("the lanes the run is given: lanes and groups, -name takes one away, none"))
                                        .option(Option("commit").takes_value().value_name("REV").help("the commit whose `CI:` trailer names a pull request's lanes (default: HEAD)"))
                                        .option(Option("dispatch").help("request what the plan requests (gh workflow run)"))
                                        .option(Option("ref").takes_value().value_name("BRANCH").help("the branch requested runs run on (default: the current one)"))
                                        .option(Option("github-output").takes_value().value_name("FILE").help("append the step's outputs: matrix, rows and each job's lane"))
                                        .option(Option("summary").takes_value().value_name("FILE").help("append the plan in Markdown")))
                        .subcommand(cmd::App("request")
                                        .description("Request lanes on GitHub (gh workflow run): ci.yml's as one run, each workflow its own; prints the runner minutes")
                                        .option(Option("ref").takes_value().value_name("BRANCH").help("the branch (default: the current one)"))
                                        .arg(Arg("selection").required().help("lanes and groups, -name takes one away (omui-devtools ci plan --lanes … says what they are)")))
                        .subcommand(cmd::App("test")
                                        .description("The workspace's tests as several `mcpp test`s, one a block of members (RC0.10: a failure's report and the disk a build takes held to a block; mcpp#945, which stopped the whole run, fixed in 2026.11.17.2): the blocks are checked to be the members each once; every block runs; `workspace result ok` when all pass")
                                        .option(Option("blocks").takes_value().value_name("N").help("how many (default: 12)"))
                                        .option(Option("exclude").takes_value().multiple().value_name("PACKAGE").help("a member left out, by its package's name or path"))
                                        .option(Option("profile").takes_value().value_name("P").help("the build profile (default: dev)"))
                                        .option(Option("list").help("say the blocks; run nothing"))))
        .subcommand(omui::devtools::eval::command())
        .subcommand(cmd::App("govern")
                        .description("Run a command within 90% of this machine: MCPP_JOBS, at most that many of its compilers and linkers at once, fewer while memory is short, at low priority (AGENTS §5)")
                        .option(Option("jobs").takes_value().value_name("N").help("how many may run at once (default: 90% of the cores and of the memory at 2.25 GB a compile)"))
                        .option(Option("normal-priority").help("not under ionice and nice"))
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
                        .option(Option("report").takes_value().value_name("FILE").help("default: target/incremental.report.xdef.json")))
        .subcommand(cmd::App("rename")
                        .description("An internal code name renamed everywhere it is a word (D-208): packages, modules, namespaces, programs, paths, docs; .agents/ kept")
                        .arg(cmd::Arg("from").required().help("the code name now (xtools)"))
                        .arg(cmd::Arg("to").required().help("the new one"))
                        .option(Option("write").help("carry it out (else: what would change)")))
        .subcommand(cmd::App("split")
                        .description("A project as a repository of its own (RC3.13): its members and all they reach, with the root's files, as a workspace")
                        .arg(cmd::Arg("project").required().help("its directory (modules/projects/omui), or a tier: foundation, framework, engine (D-231)"))
                        .option(Option("out").takes_value().value_name("DIR").help("where the tree goes (default: target/split/<project>)"))
                        .option(Option("list").help("only say the members and how many files")))
        .subcommand(cmd::App("release")
                        .description("The release channel (RC3.1): what a new project needs published, and its index entries (nothing is published)")
                        .subcommand(cmd::App("try")
                                        .description("The release tried here as a new user meets it (E1): a clean mcpp home whose index is mcpp-index with this tree's descriptors (archives seeded, nothing published), a project from a template, built, its first window")
                                        .option(Option("template").takes_value().value_name("NAME").help("app-ts (default) or app-cpp"))
                                        .option(Option("work").takes_value().value_name("DIR").help("default: target/release/try (emptied first)"))
                                        .option(Option("tag").takes_value().value_name("TEMPLATE").help("the release's tag (release index --tag)"))
                                        .option(Option("index").takes_value().value_name("DIR").help("the copy of mcpp-index it starts from (default: this home's)"))
                                        .option(Option("tool").takes_value().value_name("DIR").help("with the omui tool (release tool's directory: its omui.lua and archive), installed through xlings in a home of its own ($TMPDIR/omui-release-xlings): `omui new` and `omui run` through its player, nothing built; the time from new to the first window (E2: ≤ 60 s)")))
                        .subcommand(cmd::App("tool")
                                        .description("The omui tool as a release ships it (E2): the command, the player built with the designer (a TypeScript application of this release runs through it, nothing built) and the development host, vendored, in one directory, its archive and its xlings descriptor (omui.lua: its C library and graphics stack are xlings', red 142)")
                                        .option(Option("out").takes_value().value_name("DIR").help("default: target/release/tool (emptied first)")))
                        .subcommand(cmd::App("runtime")
                                        .description("The shared runtime as a release ships it (E3b): the player without the designer and omui-launch, vendored (xlings installs it onto its own C library), with runtime.json, as omui-runtime-<version>-<platform>/ and its archive; the xlings descriptor of every version packed (omui-runtime.lua)")
                                        .option(Option("out").takes_value().value_name("DIR").help("default: target/release/runtime (each version's directory and archive replaced, the others kept)"))
                                        .option(Option("version").takes_value().value_name("V").help("the version it ships as (default: the player's)"))
                                        .option(Option("no-pack").help("only the descriptor (and --check) from what is packed"))
                                        .option(Option("check").help("in an xlings home of its own: every version installed side by side; two applications from app-ts, one pinned to the oldest, started together each from its runtime and drawing; the way out a refusal gives, run; each package ≤ 100 KB (report: <out>/check.json)"))
                                        .option(Option("home").takes_value().value_name("DIR").help("the check's xlings home (default: $TMPDIR/omui-release-xlings, outside any project; made with `xlings self init`, this machine's downloads linked in)"))
                                        .option(Option("omui").takes_value().value_name("FILE").help("the check also runs `omui run` with this omui command in the newest application, the launcher on PATH: it starts on its runtime")))
                        .subcommand(cmd::App("index")
                                        .description("The templates' closure, its mcpp-index descriptors written to a directory, and what blocks `mcpp publish`")
                                        .option(Option("out").takes_value().value_name("DIR").help("default: target/release/index"))
                                        .option(Option("list").help("only say the closure and its blockers"))
                                        .option(Option("tag").takes_value().value_name("TEMPLATE").help("the release's tag, mcpp publish's {version} {name} {namespace} (default: v{version})"))
                                        .option(Option("index").takes_value().value_name("DIR").help("a copy of mcpp-index (default: ~/.mcpp/indices/mcpplibs)"))))
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
                        .description("Write docs/{en,zh}/platforms.md from docs/release.platforms.xdef.json (the verification table, D-201)")
                        .option(Option("check").help("only say whether the pages are current (exit 1 when not)")))
        .subcommand(cmd::App("docs")
                        .description("Write docs/{en,zh}/maturity.md, experimental.md and reference.md: every package's label, the experimental READMEs, the catalogs and contracts (plan §6.3, D-145)")
                        .option(Option("check").help("only say whether the pages are current (exit 1 when not)")))
        .subcommand(cmd::App("closure")
                        .description("Write docs/{en,zh}/closure.md: each project's closure checklist read from the tree (plan §5.1, gate 3)")
                        .option(Option("check").help("only say whether the pages are current (exit 1 when not)"))
                        .option(Option("gate").help("exit 1 while a project owes an item (the release gate)")))
        .subcommand(cmd::App("llvm")
                        .description("Compile what a change touches and its direct consumers, tests included, with CI's LLVM and profile (not run)")
                        .option(Option("since").takes_value().value_name("REF").help("the change: files differing from REF, and new ones (default: @{upstream})"))
                        .option(Option("package").short_name('p').takes_value().multiple().value_name("MEMBER").help("these members instead of the change's"))
                        .option(Option("toolchain").takes_value().value_name("T").help("default: the LLVM of CI's rows (.github/workflows/ci.yml)"))
                        .option(Option("profile").takes_value().value_name("P").help("default: ci"))
                        .option(Option("no-consumers").help("only the members themselves"))
                        .option(Option("dry").help("say what would be compiled, and how")))
        .subcommand(cmd::App("consumer")
                        .description("An out-of-tree consumer probe: a fresh project built and run as a user's, what mcpp resolved and compiled audited")
                        .arg(Arg("probe").required().help("hanim-core, hanim-api, hanim-play, omui-hanim or hanim-features"))
                        .option(Option("output").takes_value().value_name("FILE").help("the report (default under target/)"))
                        .option(Option("keep-project").help("leave the fresh project in the temporary directory"))
                        .option(Option("web").help("omui-hanim: on wasm32-emscripten too"))
                        .option(Option("openas-package").takes_value().value_name("DIR").help("hanim-play: an unpacked OpenAS binary artifact in place of the published one")))
        .subcommand(cmd::App("lint")
                        .description("The repository's rules, checked (exit 1 when a rule is broken)")
                        .option(Option("rule").takes_value().multiple().value_name("NAME").help("only these rules"))
                        .option(Option("list").help("the rules and what each checks"))
                        .option(Option("self-test").help("each rule's violation planted in a tree of its own: caught, and by it alone")))
        .subcommand(cmd::App("check")
                        .description("CI's acceptance steps: each builds what it needs, runs it and holds the result")
                        .subcommand(cmd::App("features").description("M0 ⑤: a feature selects one of two exclusive sources; both at once is refused"))
                        .subcommand(cmd::App("size")
                                        .description("A package's WebAssembly bytes as a release ships them (or, --native, this machine's program as dist builds it), by owner; held to a budget when given")
                                        .option(Option("native").help("this machine's program in the size profile (dist's code, its symbols kept), not the Web's"))
                                        .option(Option("package").short_name('p').takes_value().value_name("MEMBER").help("the program's package"))
                                        .option(Option("json").takes_value().value_name("FILE").help("the report (default: target/<package>.wasm-size.json)"))
                                        .option(Option("markdown").takes_value().value_name("FILE").help("the report as a table"))
                                        .option(Option("budget").takes_value().value_name("FILE").help("its budgets (<name>.budget.xdef.json)")))
                        .subcommand(cmd::App("templates").description("Each template rendered as mcpp new renders it, pointed at this checkout, built and tested"))
                        .subcommand(cmd::App("reflect")
                                        .description("omui.reflect's suite where the toolchain builds reflection beside C headers (V-08); a warning where not")
                                        .option(Option("profile").takes_value().value_name("P").help("default: dev")))
                        .subcommand(cmd::App("sweep")
                                        .description("Every program run headless and swept by an agent: every action performed, a dead control fails it")
                                        .option(Option("profile").takes_value().value_name("P").help("the programs' build profile")))
                        .subcommand(cmd::App("toy")
                                        .description("The games packed for Bilibili Toy and checked by Toy's own doctor (pinned)")
                                        .arg(Arg("package").help("the games (default: the fighting game and the breakout)")))
                        .subcommand(cmd::App("tools-e2e")
                                        .description("hanim's, xrender's and the timeline's commands as an Agent uses them: exit statuses and answers held")
                                        .option(Option("profile").takes_value().value_name("P")))
                        .subcommand(cmd::App("agent-e2e")
                                        .description("omui agent and omui mcp against example 07 and the Reaction lab, each a process of its own")
                                        .option(Option("profile").takes_value().value_name("P")))
                        .subcommand(cmd::App("distribution")
                                        .description("Example 01 packed for this machine: Inter alone of the fonts (at most 0.9 MB), no window system's library (B4)")
                                        .option(Option("profile").takes_value().value_name("P")))
                        .subcommand(cmd::App("android")
                                        .description("Example 01 as an APK, checked inside; on the emulator where there is KVM: started, a tap, Back")
                                        .option(Option("out").takes_value().value_name("DIR").help("the screenshots and logs (default: the system's temporary directory, omui-android)")))
                        .subcommand(cmd::App("web")
                                        .description("The Web row: the members that run on the Web tested under node, the page halves' JavaScript, the yard's bytes, the browser")
                                        .option(Option("profile").takes_value().value_name("P").help("default: wasm-test (tests read their fixtures through node)")))
                        .subcommand(cmd::App("browser").description("What only a real page can check: the page backends' suites, the shaders, host.web's frames"))
                        .subcommand(cmd::App("site")
                                        .description("The built site in a real browser: delivery within budget, accessibility, structure, fonts, catalog, workspaces, motion, first views, loading, font sharing")
                                        .option(Option("site").takes_value().value_name("DIR").help("the built site (default: target/site)"))
                                        .arg(Arg("check").help("these checks (default: every one but transition): perf a11y structure fonts catalog workspace reactions motion presentation loading font-sharing transition")))
                        .subcommand(cmd::App("visitor-path")
                                        .description("The built site paired with an agent through omui connect and operated by omui page; the bridge's end seen by the page")
                                        .option(Option("site").takes_value().value_name("DIR").help("the built site (default: target/site)"))
                                        .option(Option("omui").takes_value().value_name("PATH").help("this omui program rather than omui.cli built here"))
                                        .option(Option("url").takes_value().value_name("URL").help("a deployed site (GitHub Pages) rather than the one built here"))
                                        .option(Option("allow-local").help("the person allows the page to reach this machine (the browser's local network access permission)")))
                        .subcommand(cmd::App("live-page")
                                        .description("A page as it ships, in a real browser through window.omui: its design replayed in place, a look and a view document committed, kept across a reload, exported and imported")
                                        .option(Option("page").takes_value().value_name("DIR").help("the built page (default: example 13's, omui build --web)")))
                        .subcommand(cmd::App("site-page")
                                        .description("omui's site page as it ships, in a real browser: its delivery within budget, then the Live Design key, Copy, the visitor's agent connected through omui connect, the bridge stopped")
                                        .option(Option("page").takes_value().value_name("DIR").help("the built page (default: site/page's, omui build --web)"))
                                        .option(Option("omui").takes_value().value_name("PATH").help("this omui program rather than omui.cli built here"))
                                        .arg(Arg("part").help("these parts (default: both): delivery path")))
                        .subcommand(cmd::App("dist-toy-loading")
                                        .description("omui.gen's dist_toy tests, then the generated loader run against the browser's own streams, failures and retries")
                                        .option(Option("fixture").takes_value().value_name("FILE").help("this generated index.html, the tests not run")))
                        .subcommand(cmd::App("atspi")
                                        .description("Examples 03 and 07 built, then read and operated through AT-SPI on a private session bus")
                                        .option(Option("profile").takes_value().value_name("P").help("the examples' build profile"))))
        .subcommand(cmd::App("media")
                        .description("The showcases' and the site's README media: what their tests draw, copied in (or, --check, held to it)")
                        .option(Option("check").help("hold the committed media to what the tests drew (after the tests)"))
                        .option(Option("showcases").help("every showcase, without the site"))
                        .arg(Arg("show").help("showcase directory names, or site (default: every showcase and the site)")))
        .subcommand(cmd::App("browser")
                        .description("A wasm test program (mcpp's wasm-browser profile) run in a real page of headless Chromium; its lines relayed, its exit and suites judged")
                        .option(Option("chrome").takes_value().value_name("PATH").help("default: OMUI_CHROME, Playwright's headless shell, or one on PATH"))
                        .option(Option("timeout").takes_value().value_name("SECONDS").help("default: 300"))
                        .option(Option("flag").takes_value().multiple().value_name("SWITCH").help("a switch for Chromium (--flag=--use-angle=vulkan)"))
                        .option(Option("preload").takes_value().multiple().value_name("DIR:/DEST").help("every file of DIR at /DEST before main()"))
                        .option(Option("headed").help("a window on the display there is (a real GPU's Vulkan), not headless"))
                        .arg(Arg("program").required().help("after --: PROGRAM.js and its arguments")))
        .subcommand(cmd::App("site")
                        .description("The website: built for the Web and pre-rendered, verified, split by what its first screen runs, profiled; a page driven")
                        .subcommand(cmd::App("build").description("omui.site packed for the Web into target/site, its openings, places and agent guide written, split, verified"))
                        .subcommand(cmd::App("verify")
                                        .description("The built page carries what it must (loader, static places, fonts, canvas, the agent's files); its sizes said")
                                        .arg(Arg("dir").help("default: target/site")))
                        .subcommand(cmd::App("split")
                                        .description("The program cut in two by what its first screen runs (wasm-split, a profile in each language, omui-gen dist-toy-split)")
                                        .arg(Arg("dir").help("default: target/site"))
                                        .option(Option("routes").takes_value().value_name("R,…").help("the first screens profiled, by route (default: the site's two languages, \"\" and /zh; a page of one language: \"\")")))
                        .subcommand(cmd::App("profile")
                                        .description("A page with wasm-split's instrumented program used at its first screen; the functions that ran written")
                                        .option(Option("route").takes_value().value_name("R").help("the route, e.g. /zh"))
                                        .option(Option("idle").takes_value().value_name("MS").help("idle at the end (default: 4000)"))
                                        .arg(Arg("dir").required().help("the packed page"))
                                        .arg(Arg("profile").required().help("the profile written")))
                        .subcommand(cmd::App("drive")
                                        .description("A page in a real browser in real time: left to run, a script evaluated, captured; its logs printed")
                                        .option(Option("flag").takes_value().multiple().value_name("SWITCH").help("a switch for Chromium instead of --disable-gpu"))
                                        .option(Option("click").takes_value().value_name("X,Y").help("a person's click there first"))
                                        .option(Option("no-script").help("the page as a reader without JavaScript has it (the script still evaluated)"))
                                        .arg(Arg("url").required().help("an address, or a built page's directory (served as Pages serves it)"))
                                        .arg(Arg("out").required().help("the PNG"))
                                        .arg(Arg("seconds").help("default: 6"))
                                        .arg(Arg("width").help("default: 1280"))
                                        .arg(Arg("height").help("default: 800"))
                                        .arg(Arg("script").help("evaluated after the seconds (awaited; window.omui is the program's agent interface)"))))
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
                                        .option(Option("dry-run").help("with --ledger: print the verdicts, write nothing")))
                        .subcommand(omui::devtools::bench::apps_command_line())
                        .subcommand(cmd::App("material3")
                                        .description("Material 3's controls in four states (rest, hover, pressed, focus), captured from their page as the competitor's side of the controls' review")
                                        .arg(Arg("out").required().help("OUT/<control>-<state>.png"))))
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

// RC0.10: the workspace's tests by blocks of members — the same members and cases as the whole run.
int blocks_test(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto manifest = mcpplibs::os::fs::read(root / "mcpp.toml");
    if (!manifest) {
        std::cerr << "omui-devtools ci test: no mcpp.toml in the workspace (--root names it)\n";
        return 2;
    }
    const auto excluded = options.option_or_empty("exclude").values;
    std::vector<std::string> members;
    for (const auto& path : ws::listed(*manifest)) {
        const auto own = mcpplibs::os::fs::read(root / mcpplibs::os::fs::from_utf8(path) / "mcpp.toml");
        const std::string package = own ? rec::package_of(*own) : std::string{};
        if (std::ranges::contains(excluded, path) || (!package.empty() && std::ranges::contains(excluded, package))) continue;
        members.push_back(path);
    }
    std::size_t count = 12;
    if (const auto given = options.value("blocks")) {
        const auto parsed = std::from_chars(given->data(), given->data() + given->size(), count);
        if (parsed.ec != std::errc{} || count == 0) {
            std::cerr << std::format("omui-devtools ci test: --blocks takes a positive number, not `{}`\n", *given);
            return 2;
        }
    }
    const auto split = ws::blocks(members, count);
    if (const auto problem = ws::cover_problem(members, split); !problem.empty()) {
        std::cerr << "omui-devtools ci test: the blocks are not the workspace: " << problem << '\n';
        return 2;
    }
    const std::string profile = options.value("profile").value_or("dev");
    std::println("{} members in {} blocks (the workspace less {} excluded)", members.size(), split.size(), excluded.size());
    std::vector<std::size_t> failed;
    for (std::size_t index = 0; index < split.size(); ++index) {
        std::vector<std::string> args{"test", "--profile", profile};
        for (const auto& member : split[index]) {
            args.push_back("-p");
            args.push_back(member);
        }
        std::println("== block {}/{}: {}", index + 1, split.size(), split[index].front() + " … " + split[index].back());
        std::cout.flush();  // before mcpp writes its own (synchronised with C's stdout)
        if (options.is_flag_set("list")) {
            for (const auto& member : split[index]) std::println("   {}", member);
            continue;
        }
        const auto ran = omui::devtools::run::program(root, "mcpp", args, {.shown = true});
        if (!ran || ran->status != 0) {
            failed.push_back(index + 1);
            std::println("== block {}/{} failed{}", index + 1, split.size(), ran ? "" : ": " + ran.error());
        }
    }
    if (options.is_flag_set("list")) return 0;
    if (!failed.empty()) {
        std::string named;
        for (const auto block : failed) named += std::format("{}{}", named.empty() ? "" : ", ", block);
        std::println("blocks result FAILED: block {} of {}", named, split.size());
        return 1;
    }
    std::println("blocks result ok: {} blocks\nworkspace result ok", split.size());
    return 0;
}

int ci_lanes(const cmd::ParsedArgs& given, std::string_view command, const fs::path& root);

int ci(const cmd::ParsedArgs& options, const fs::path& root) {
    if (!options.has_subcommand()) {
        std::cerr << "usage: omui-devtools ci [--json] run ID | log FILE [--jobs FILE] | plan [--lanes S] [--commit REV] | request SELECTION… | local [MEMBER…] | test [--blocks N] [--exclude P]… [--profile P] [--list]\n";
        return 2;
    }
    if (options.subcommand_name() == "test") return blocks_test(options.subcommand()->get(), root);
    if (options.subcommand_name() == "plan" || options.subcommand_name() == "request")
        return ci_lanes(options.subcommand()->get(), options.subcommand_name(), root);
    if (options.subcommand_name() == "local") {
        const auto& given = options.subcommand()->get();
        std::vector<std::string> members;
        for (std::size_t index = 0; index < given.positional_count(); ++index) members.push_back(given.positional(index));
        return omui::devtools::check::matrix(root, members);
    }
    const std::string status_name = options.value("status").value_or(".agents/docs/STATUS.md");
    const auto status = mcpplibs::os::fs::read(options.value("status") ? mcpplibs::os::fs::from_utf8(status_name) : root / ".agents/docs/STATUS.md");
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
        auto read = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(run));
        if (!read) {
            std::cerr << std::format("omui-devtools ci: cannot read {}\n", run);
            return 2;
        }
        log = std::move(*read);
        if (auto file = sub.value("jobs")) {
            auto text = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(*file));
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

// `git ARGS…` in the workspace: its output.
std::expected<std::string, std::string> git_text(const fs::path& root, std::vector<std::string> args) {
    const auto program = mcpplibs::os::env::find_executable("git");
    if (!program) return std::unexpected("`git` is not on PATH");
    mcpplibs::os::process::spawn_options options;
    options.program = *program;
    options.args = {"-C", mcpplibs::os::fs::utf8(root)};
    options.args.insert(options.args.end(), args.begin(), args.end());
    auto ran = mcpplibs::os::process::run(std::move(options));
    if (!ran) return std::unexpected(std::format("git did not start: {}", ran.error().message()));
    if (ran->status.shell_code() != 0) return std::unexpected(std::format("git {}: {}", args.front(), ran->err));
    return std::move(ran->out);
}

// `git ARGS…` in the workspace: its output's lines.
std::expected<std::vector<std::string>, std::string> git_lines(const fs::path& root, std::vector<std::string> args) {
    auto text = git_text(root, std::move(args));
    if (!text) return std::unexpected(text.error());
    std::vector<std::string> lines;
    for (const auto part : std::views::split(std::string_view(*text), '\n'))
        if (!std::string_view(part.begin(), part.end()).empty()) lines.emplace_back(part.begin(), part.end());
    return lines;
}

// omui-devtools ci plan | request (D-254): which of CI's lanes a run tests, from .github/ci.lanes.xdef.json.
int ci_lanes(const cmd::ParsedArgs& given, std::string_view command, const fs::path& root) {
    const auto text = mcpplibs::os::fs::read(root / ".github/ci.lanes.xdef.json");
    if (!text) {
        std::cerr << "omui-devtools ci: cannot read .github/ci.lanes.xdef.json (the lanes' catalogue)\n";
        return 2;
    }
    const auto catalogue = lanes::read(*text);
    if (!catalogue) {
        std::cerr << std::format("omui-devtools ci: .github/ci.lanes.xdef.json: {}\n", catalogue.error());
        return 2;
    }
    const bool request = command == "request";
    std::string selection, message;
    if (request) {
        for (std::size_t index = 0; index < given.positional_count(); ++index) selection += std::format("{} ", given.positional(index));
    } else {
        selection = given.value("lanes").value_or("");
        auto read = git_text(root, {"log", "-1", "--format=%B", given.value("commit").value_or("HEAD")});
        if (!read) {
            std::cerr << std::format("omui-devtools ci plan: the commit's message: {}\n", read.error());
            return 2;
        }
        message = std::move(*read);
    }
    const std::string event = request ? "request" : given.value("event").value_or("pull_request");
    const auto planned = lanes::plan(*catalogue, event, selection, message);
    if (!planned) {
        std::cerr << std::format("omui-devtools ci {}: {}\n", command, planned.error());
        return 2;
    }
    const std::string described = lanes::describe(*catalogue, *planned);
    std::cout << described;
    const auto append = [](const std::string& path, const std::string& content) {
        std::ofstream out(path, std::ios::app | std::ios::binary);
        out << content;
        return static_cast<bool>(out);
    };
    if (const auto output = given.value("github-output"); output && !append(std::string(*output), lanes::outputs(*catalogue, *planned))) {
        std::cerr << "omui-devtools ci plan: cannot write the step's outputs\n";
        return 2;
    }
    if (const auto summary = given.value("summary"); summary) append(std::string(*summary), described);
    if (!request && !given.is_flag_set("dispatch")) return 0;
    std::string ref(given.value("ref").value_or(""));
    if (ref.empty()) {
        const auto branch = git_lines(root, {"rev-parse", "--abbrev-ref", "HEAD"});
        if (branch && !branch->empty() && branch->front() != "HEAD") ref = branch->front();
    }
    if (ref.empty() && (!planned->requested.empty() || !planned->workflows.empty())) {
        std::cerr << "omui-devtools ci: no branch to request the runs on (--ref names it)\n";
        return 2;
    }
    int failed = 0;
    const auto run = [&](std::vector<std::string> args, std::string_view what) {
        auto ran = gh(std::move(args));
        if (ran) {
            std::cout << std::format("requested {} on {}\n", what, ref);
        } else {
            std::cerr << std::format("omui-devtools ci: requesting {}: {}\n", what, ran.error());
            ++failed;
        }
    };
    if (!planned->requested.empty()) {
        std::string names;
        for (const auto& name : planned->requested) names += (names.empty() ? "" : " ") + name;
        run({"workflow", "run", "ci.yml", "--ref", ref, "-f", "lanes=" + names}, std::format("ci.yml ({})", names));
    }
    for (const auto& workflow : planned->workflows) run({"workflow", "run", workflow, "--ref", ref}, workflow);
    return failed > 0 ? 1 : 0;
}

// The LLVM CI's rows build with, as .github/workflows/ci.yml names it ("llvm@23.1.3").
std::string ci_llvm(const fs::path& root) {
    const auto text = mcpplibs::os::fs::read(root / ".github/workflows/ci.yml");
    if (!text) return {};
    const auto at = text->find("llvm@");
    if (at == std::string::npos) return {};
    auto end = at + 5;
    while (end < text->size() && (std::isdigit(static_cast<unsigned char>((*text)[end])) || (*text)[end] == '.')) ++end;
    return text->substr(at, end - at);
}

int llvm(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto manifest = mcpplibs::os::fs::read(root / "mcpp.toml");
    if (!manifest) {
        std::cerr << "omui-devtools llvm: no mcpp.toml in the workspace (--root names it)\n";
        return 2;
    }
    const auto paths = ws::listed(*manifest);
    std::vector<ws::Member> all;
    for (const auto& path : paths) {
        const auto own = mcpplibs::os::fs::read(root / mcpplibs::os::fs::from_utf8(path) / "mcpp.toml");
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
    std::cout << std::format("{} member(s) changed, {} with their direct consumers; {}\n", chosen.size(), picked.size(), toolchain);
    for (const auto& path : picked) std::cout << "  " << path << (std::ranges::contains(chosen, path) ? "" : "  (consumer)") << '\n';
    std::string shown = "omui-devtools govern -- mcpp";
    for (const auto& arg : args) shown += " " + arg;
    std::cout << shown << '\n';
    if (options.is_flag_set("dry")) return 0;
    // Within 90% of the machine (AGENTS §5).
    namespace held = omui::devtools::govern;
    held::Options given{.jobs = omui::devtools::run::share_of_machine(), .cwd = root};
    given.command = {"mcpp"};
    given.command.insert(given.command.end(), args.begin(), args.end());
    const auto status = held::run(given);
    if (!status) {
        std::cerr << "omui-devtools llvm: " << status.error() << '\n';
        return 2;
    }
    return *status;
}

int govern(const cmd::ParsedArgs& options) {
    namespace held = omui::devtools::govern;
    held::Options given;
    const auto number = [](std::string_view text, auto& value) {
        const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), value);
        return failed == std::errc{} && end == text.data() + text.size();
    };
    given.jobs = omui::devtools::run::share_of_machine();
    if (const auto jobs = options.value("jobs"); jobs && (!number(*jobs, given.jobs) || given.jobs < 1)) {
        std::cerr << "omui-devtools govern: --jobs takes a count of at least 1\n";
        return 2;
    }
    given.low_priority = !options.is_flag_set("normal-priority");
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
        const auto manifest = mcpplibs::os::fs::read(directory / "mcpp.toml");
        if (!manifest) {
            std::cerr << std::format("omui-devtools api: no mcpp.toml in {}\n", member);
            return 2;
        }
        const std::string now = omui::devtools::api::surface(directory, omui::devtools::api::interface_globs(*manifest));
        const fs::path file = directory / "api.omui.txt";
        if (options.is_flag_set("check")) {
            if (mcpplibs::os::fs::read(file) != now) {
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

// A number, or nothing.
std::optional<double> number_of(std::string_view text) {
    double value = 0;
    const auto [end, failed] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (failed != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

int site_command(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace site = omui::devtools::site;
    if (!options.has_subcommand()) {
        std::cerr << "usage: omui-devtools site build | verify [DIR] | split [DIR] [--routes R,…] | profile DIR PROFILE [--route R] [--idle MS] | drive URL OUT [SECONDS] [WIDTH] [HEIGHT] [SCRIPT]\n";
        return 2;
    }
    const auto& given = options.subcommand()->get();
    const auto directory = [&] { return given.positional_count() > 0 ? mcpplibs::os::fs::from_utf8(given.positional(0)) : root / "target/site"; };
    if (options.subcommand_name() == "build") return site::build(root);
    if (options.subcommand_name() == "verify") return site::verify(directory());
    if (options.subcommand_name() == "split") {
        std::vector<std::string> routes{"", "/zh"};
        if (const auto asked = given.value("routes")) {
            routes.clear();
            for (auto part : std::views::split(std::string_view(*asked), ',')) routes.emplace_back(part.begin(), part.end());
            if (routes.empty()) routes.emplace_back();  // "": the page's own first screen
        }
        return site::split(root, directory(), routes);
    }
    if (options.subcommand_name() == "profile") {
        site::Profile asked{.site = mcpplibs::os::fs::from_utf8(given.positional(0)), .out = mcpplibs::os::fs::from_utf8(given.positional(1)),
                            .route = given.value("route").value_or("")};
        if (const auto idle = given.value("idle")) {
            const auto value = number_of(*idle);
            if (!value || *value < 0) {
                std::cerr << "omui-devtools site profile: --idle takes milliseconds\n";
                return 2;
            }
            asked.idle_ms = static_cast<int>(*value);
        }
        return site::profile(asked);
    }
    site::Drive asked{.url = given.positional(0), .out = mcpplibs::os::fs::from_utf8(given.positional(1))};
    const std::array<std::string_view, 3> numbers{"seconds", "width", "height"};
    for (std::size_t index = 0; index < numbers.size(); ++index) {
        if (given.positional_count() <= index + 2) break;
        const auto value = number_of(given.positional(index + 2));
        if (!value || *value < 0) {
            std::cerr << std::format("omui-devtools site drive: {} is a number\n", numbers[index]);
            return 2;
        }
        if (index == 0) asked.seconds = *value;
        else if (index == 1) asked.width = static_cast<int>(*value);
        else asked.height = static_cast<int>(*value);
    }
    if (given.positional_count() > 5) asked.script = given.positional(5);
    asked.flags = given.option_or_empty("flag").values;
    if (const auto click = given.value("click")) {
        const auto comma = click->find(',');
        const auto x = number_of(click->substr(0, comma)), y = comma == std::string::npos ? std::nullopt : number_of(click->substr(comma + 1));
        if (!x || !y) {
            std::cerr << "omui-devtools site drive: --click takes X,Y\n";
            return 2;
        }
        asked.click = std::pair{*x, *y};
    }
    asked.no_script = given.is_flag_set("no-script");
    return site::drive(asked);
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
        const auto judged = lead::verdicts(mcpplibs::os::fs::read(root / lead::metrics_path).value_or(""), mcpplibs::os::fs::read(fs::path(sub.positional(0))).value_or(""),
                                           mcpplibs::os::fs::read(ledger).value_or(""), lead::context_here(root), skipped);
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

// The copy of mcpp-index this machine's home keeps (`--index` names another).
fs::path mcpp_index_of(const cmd::ParsedArgs& options) {
    if (const auto given = options.value("index")) return mcpplibs::os::fs::from_utf8(*given);
    if (const auto home = mcpplibs::os::env::get("MCPP_HOME"); home && !home->empty())
        return mcpplibs::os::fs::from_utf8(*home) / "indices" / "mcpplibs";
    if (const auto user = mcpplibs::os::env::get(mcpplibs::os::env::get("HOME") ? "HOME" : "USERPROFILE"); user && !user->empty())
        return mcpplibs::os::fs::from_utf8(*user) / ".mcpp" / "indices" / "mcpplibs";
    return {};
}

// `release index`: the templates' closure, what blocks publishing it, and its mcpp-index descriptors.
int release_index(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace release = omui::devtools::release;
    const auto made = release::closure(root);
    if (!made) {
        std::cerr << "omui-devtools release index: " << made.error() << '\n';
        return 2;
    }
    const fs::path index = mcpp_index_of(options);
    const auto missing = release::missing_from(index, made->external);

    std::map<std::string, std::map<std::string, int>> counts;  // project → maturity → packages
    json listed = json::array();
    for (const auto& package : made->packages) {
        const std::string project = package.name.substr(0, package.name.find('.'));
        ++counts[project][package.maturity.empty() ? "unlabelled" : package.maturity];
        std::println("  {:<34} {:<12} {:<6} {}{}", package.name,
                     package.maturity.empty() ? "-" : package.maturity,
                     package.built ? "built" : "feature", package.path,
                     package.outside.empty() ? ""
                                             : std::format("  [{} strings leave its directory]",
                                                           package.outside.size()));
        listed.push_back({{"name", package.name},
                          {"path", package.path},
                          {"version", package.version},
                          {"maturity", package.maturity},
                          {"built", package.built},
                          {"outside", package.outside},
                          {"external", package.external},
                          {"descriptor", release::descriptor_path(package)},
                          {"archive", std::format("{}-{}.tar.gz", package.name, package.version)}});
    }
    const auto built = std::ranges::count_if(made->packages, &release::Package::built);
    std::println("roots: {}", made->roots.size());
    std::println("closure: {} packages ({} built with a template's features, {} reached only by "
                 "another feature)",
                 made->packages.size(), built,
                 made->packages.size() - static_cast<std::size_t>(built));
    for (const auto& [project, by_maturity] : counts) {
        std::string line;
        for (const auto& [maturity, count] : by_maturity)
            line += std::format(" {} {}", count, maturity);
        std::println("  {}:{}", project, line);
    }
    std::println("external: {} ({} not in the index at {})", made->external.size(), missing.size(),
                 mcpplibs::os::fs::utf8(index));
    for (const auto& named : missing)
        std::println("  missing {}", named);
    for (const auto& problem : made->problems)
        std::println("problem: {}", problem);
    if (options.is_flag_set("list"))
        return made->problems.empty() ? 0 : 1;

    // Each member's archive, its sum and its descriptor are mcpp publish's own (2026.11.17.2: by identity, siblings at their
    // own versions; mcpp#947): a dry run over the closure — nothing leaves this machine — then laid out as the index is.
    // target/dist is where mcpp publish writes, a real publish's archives too: cleared only when empty or this command's
    // own (its mark), else left alone and the run refused.
    std::error_code error;
    const fs::path dist = root / "target" / "dist";
    const fs::path mark = dist / ".omui-release-index";
    if (fs::exists(dist, error) && !fs::is_empty(dist, error) && !fs::exists(mark, error)) {
        std::cerr << std::format("omui-devtools release index: {} holds what another publish wrote; move it away (or remove it) and run again\n",
                                 mcpplibs::os::fs::utf8(dist));
        return 1;
    }
    fs::remove_all(dist, error);  // what this run writes, nothing an earlier one left
    std::vector<std::string> args{"publish", "--dry-run", "--allow-dirty"};
    if (const auto tag = options.value("tag")) args.insert(args.end(), {"--tag", *tag});
    for (const auto& package : made->packages) args.insert(args.end(), {"-p", package.name});
    const auto published = omui::devtools::run::program(root, "mcpp", args, {.merged = true});
    if (!published || published->status != 0) {
        const std::string said = published ? published->out : published.error();
        std::cerr << "omui-devtools release index: mcpp publish --dry-run: " << said.substr(said.size() > 3000 ? said.size() - 3000 : 0) << '\n';
        return 1;
    }
    const fs::path out = options.value("out") ? mcpplibs::os::fs::from_utf8(*options.value("out"))
                                              : root / "target" / "release" / "index";
    std::ofstream(mark, std::ios::binary) << "written by omui-devtools release index (a dry run); safe to remove\n";
    fs::remove_all(out, error);
    // Its sums, one line an archive, a file a release tag (SHA256SUMS, or SHA256SUMS-<tag> when the closure spans several
    // versions: spec's is its own): each of the closure's archives in one of them, or the run is not the whole of it.
    std::string sums;
    std::vector<fs::path> sum_files;
    for (const auto& entry : fs::directory_iterator(dist, error))
        if (mcpplibs::os::fs::utf8(entry.path().filename()).starts_with("SHA256SUMS")) {
            sum_files.push_back(entry.path());
            sums += mcpplibs::os::fs::read(entry.path()).value_or("");
        }
    for (const auto& package : made->packages)
        if (!sums.contains(std::format("  {}-{}.tar.gz", package.name, package.version))) {
            std::cerr << std::format("omui-devtools release index: mcpp publish wrote no sum for {}-{}.tar.gz\n", package.name, package.version);
            return 1;
        }
    for (const auto& package : made->packages) {
        const fs::path file = out / mcpplibs::os::fs::from_utf8(release::descriptor_path(package));
        fs::create_directories(file.parent_path(), error);
        fs::copy_file(dist / (release::descriptor_name(package.name) + ".lua"), file, fs::copy_options::overwrite_existing, error);
        if (error) {
            std::cerr << std::format("omui-devtools release index: {}.lua from mcpp publish: {}\n", release::descriptor_name(package.name), error.message());
            return 1;
        }
    }
    for (const auto& file : sum_files)
        if (fs::copy_file(file, out / file.filename(), fs::copy_options::overwrite_existing, error); error) {
            std::cerr << std::format("omui-devtools release index: {}: {}\n", mcpplibs::os::fs::utf8(file.filename()), error.message());
            return 1;
        }
    const json summary{{"roots", made->roots},
                       {"packages", listed},
                       {"external", made->external},
                       {"missing", missing},
                       {"problems", made->problems}};
    std::ofstream(out / "closure.json", std::ios::binary) << summary.dump(2) << '\n';
    std::println("written: {} descriptors and closure.json under {}", made->packages.size(),
                 mcpplibs::os::fs::utf8(out));
    return made->problems.empty() ? 0 : 1;
}

// What a directory holds, in bytes.
std::uintmax_t bytes_under(const fs::path& directory) {
    std::uintmax_t total = 0;
    std::error_code error;
    for (auto it = fs::recursive_directory_iterator(directory, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator(); it.increment(error))
        if (it->is_regular_file(error) && !it->is_symlink(error)) total += it->file_size(error);
    return total;
}

// `release try` (E1): the release tried on this machine as a new user meets it. A clean mcpp home whose default
// index is mcpp-index with this tree's descriptors in it — their archives seeded into the home's store, as mcpp's
// docs say a payload is used before it is published (11): nothing leaves this machine — then a project from a
// template, built, and its first window drawn (SDL's offscreen driver). Its report: <work>/try.json.
// The omui tool as a release ships it (E2): the command; the player built with the designer, which `omui dev` and
// `omui run` run a TypeScript application of this release through (nothing built); the development host — each
// packed as mcpp packs a program (dist, stripped, vendored: its libraries with it, not the C library), in one
// directory (bin/, share/, licenses/), its archive and its xlings descriptor (omui.lua: xlings installs it onto its
// own C library and graphics stack, red 142).
int release_tool(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace run = omui::devtools::run;
    namespace release = omui::devtools::release;
    const fs::path out = options.value("out") ? fs::absolute(mcpplibs::os::fs::from_utf8(*options.value("out"))) : root / "target/release/tool";
    struct Part {
        std::string_view program, member, features;
    };
    constexpr std::array<Part, 3> parts{{{"omui", "modules/projects/omui/tools/cli", ""},
                                         {"omui-player", "modules/projects/omui/entry/player", "design"},
                                         {"omui-devhost", "modules/projects/omui/entry/devhost", ""}}};
    // The release's version: the command's package's (the player it ships says the same).
    std::string version;
    for (const auto line : std::views::split(mcpplibs::os::fs::read(root / parts[0].member / "mcpp.toml").value_or(""), '\n'))
        if (const std::string_view text(line.begin(), line.end()); version.empty() && text.starts_with("version"))
            if (const auto open = text.find('"'), close = text.rfind('"'); open != std::string_view::npos && close > open)
                version = std::string(text.substr(open + 1, close - open - 1));
    std::error_code error;
    fs::remove_all(out, error);
    fs::create_directories(out, error);
    fs::path dir;
    for (const Part& part : parts) {
        // Vendored (red 142): no C library in it; xlings installs it onto its own (release::xlings_descriptor).
        const auto packed = release::pack_program(root, part.program, part.member, part.features);
        if (!packed) {
            std::cerr << std::format("omui-devtools release tool: {}\n", packed.error());
            return 1;
        }
        const fs::path made = *packed;
        if (dir.empty()) {
            // <package>-<version>-<triple>: the triple the programs run on
            const std::string name = mcpplibs::os::fs::utf8(made.filename()), prefix = std::format("-{}-", version);
            const auto at = name.find(prefix);
            dir = out / std::format("omui-{}-{}", version, at == std::string::npos ? std::string("unknown") : name.substr(at + prefix.size()));
        }
        fs::copy(made, dir, fs::copy_options::recursive | fs::copy_options::skip_existing, error);
        if (error) {
            std::cerr << std::format("omui-devtools release tool: {}: {}\n", mcpplibs::os::fs::utf8(made), error.message());
            return 1;
        }
    }
    fs::create_directories(dir / "licenses/omui", error);
    fs::copy_file(root / "LICENSE", dir / "licenses/omui/LICENSE", fs::copy_options::overwrite_existing, error);
    // What the player it ships says it is: this release (the command runs an application through it only then).
    // On the C library xlings installs it onto.
    const auto loader = release::xlings_loader(dir);
    if (!loader) {
        std::cerr << std::format("omui-devtools release tool: {}\n", loader.error());
        return 1;
    }
    std::vector<std::string> asked(loader->begin() + 1, loader->end());
    asked.insert(asked.end(), {mcpplibs::os::fs::utf8(dir / "bin/omui-player"), "--version"});
    const auto said = run::program(root, loader->front(), std::move(asked), {.cwd = root, .merged = true});
    std::string answered = said ? said->out : said.error();
    while (!answered.empty() && (answered.back() == '\n' || answered.back() == '\r')) answered.pop_back();
    if (!said || said->status != 0 || answered != version) {
        std::cerr << std::format("omui-devtools release tool: bin/omui-player says `{}`, the release is {}\n", answered, version);
        return 1;
    }
    const fs::path archive = out / std::format("{}.tar.gz", mcpplibs::os::fs::utf8(dir.filename()));
    // No hard links: xlings extracts them with link(2), which some systems refuse.
    if (auto packed = run::output(root, "tar", {"--hard-dereference", "-C", mcpplibs::os::fs::utf8(out), "-czf", mcpplibs::os::fs::utf8(archive), mcpplibs::os::fs::utf8(dir.filename())});
        !packed) {
        std::cerr << std::format("omui-devtools release tool: tar: {}\n", packed.error());
        return 1;
    }
    // Its sha256 and its xlings descriptor (omui.lua): what its programs ask of the machine, read from them.
    const std::string sha256 = mlibs::hash::sha256_hex(mcpplibs::os::fs::read(archive).value_or(""));
    (void)mcpplibs::os::fs::write(fs::path(archive).concat(".sha256"), std::format("{}  {}\n", sha256, mcpplibs::os::fs::utf8(archive.filename())));
    release::XlingsRelease described{
        .name = "omui",
        .description = "omui: the command, the player a TypeScript application of this release runs through, and the development host",
        .about = "omui: the tool of an omui release (E2). Written by `omui-devtools release tool`: `omui new`, `omui dev` and\n"
                 "`omui run` of a TypeScript application through the player it ships, nothing compiled.",
        .programs = {"omui", "omui-player", "omui-devhost"},
        .categories = {"gui", "tools"},
        .keywords = {"omui", "openmui", "ui", "agent"},
        .versions = {{.version = version, .url = release::release_url(version, mcpplibs::os::fs::utf8(archive.filename())), .sha256 = sha256}},
        .wants = release::wants_of(dir),
    };
    // The omui command makes, builds and packs through mcpp (D-150): the workspace's mcpp, at least.
    const nlohmann::json workspace = nlohmann::json::parse(mcpplibs::os::fs::read(root / ".xlings.json").value_or("{}"), nullptr, false);
    if (!workspace.is_discarded() && workspace.contains("workspace") && workspace["workspace"].contains("mcpp"))
        described.starts.push_back(std::format("xim:mcpp@>={}", workspace["workspace"]["mcpp"].get<std::string>()));
    if (auto written = mcpplibs::os::fs::write(out / "omui.lua", release::xlings_descriptor(described)); !written) {
        std::cerr << std::format("omui-devtools release tool: omui.lua: {}\n", written.error().message());
        return 1;
    }
    for (const Part& part : parts)
        std::println("  bin/{:13} {:>10} B", part.program, fs::file_size(dir / "bin" / std::string(part.program), error));
    std::string wanted = described.wants.glibc.empty() ? std::string() : std::format("xim:glibc@>={}", described.wants.glibc);
    for (const auto& package : described.wants.packages) wanted += std::format("{}{}", wanted.empty() ? "" : ", ", package);
    for (const auto& package : described.starts) wanted += std::format("{}{}", wanted.empty() ? "" : ", ", package);
    std::println("release tool: {} ({} B, archive {} B); omui.lua wants {}{}", mcpplibs::os::fs::utf8(dir), bytes_under(dir), fs::file_size(archive, error), wanted,
                 described.wants.graphics ? ", xim:graphics" : "");
    return 0;
}

int release_try(const cmd::ParsedArgs& options, const fs::path& root) {
    namespace release = omui::devtools::release;
    namespace run = omui::devtools::run;
    using clock = std::chrono::steady_clock;
    const std::string template_name(options.value("template").value_or("app-ts"));
    const fs::path work = options.value("work") ? fs::absolute(mcpplibs::os::fs::from_utf8(*options.value("work")))
                                                : fs::absolute(root / "target/release/try");
    const fs::path base = mcpp_index_of(options);
    json report{{"commit", run::commit(root)}, {"date", run::today()}, {"template", template_name}, {"steps", json::array()}};
    const auto written = [&] { std::ofstream(work / "try.json", std::ios::binary) << report.dump(2) << '\n'; };
    const auto failed = [&](std::string_view step, const std::string& why) {
        report["steps"].push_back({{"step", step}, {"ok", false}, {"why", why}});
        written();
        std::cerr << std::format("omui-devtools release try: {}: {}\n", step, why.substr(why.size() > 4000 ? why.size() - 4000 : 0));
        return 1;
    };
    const auto passed = [&](std::string_view step, clock::time_point since, json more = json::object()) {
        const double seconds = std::chrono::duration<double>(clock::now() - since).count();
        more["step"] = step;
        more["ok"] = true;
        more["seconds"] = std::round(seconds * 10) / 10;
        report["steps"].push_back(more);
        std::println("ok   {} ({:.1f} s)", step, seconds);
    };
    std::error_code error;

    // This tree's archives and descriptors: release index (a dry run of mcpp publish).
    if (const int indexed = release_index(options, root); indexed != 0) return indexed;
    const fs::path descriptors = root / "target/release/index", dist = root / "target/dist";
    const auto closure = json::parse(mcpplibs::os::fs::read(descriptors / "closure.json").value_or("{}"), nullptr, false);
    if (closure.is_discarded() || !closure.contains("packages")) return failed("index", "release index wrote no closure.json");
    fs::remove_all(work, error);
    fs::create_directories(work, error);

    // The stand-in for the index a release will be in: this machine's copy of mcpp-index, this tree's descriptors
    // added as mcpp publish wrote them — a git repository the clean home reads as its default index.
    auto since = clock::now();
    const fs::path index = work / "index";
    if (base.empty() || !fs::exists(base / "pkgs", error)) return failed("index", std::format("no copy of mcpp-index at {} (--index names one)", mcpplibs::os::fs::utf8(base)));
    fs::copy(base, index, fs::copy_options::recursive, error);
    if (error) return failed("index", std::format("copying {}: {}", mcpplibs::os::fs::utf8(base), error.message()));
    fs::remove_all(index / ".git", error);
    for (const auto& package : closure["packages"]) {
        const std::string relative = package.value("descriptor", "");
        fs::create_directories((index / relative).parent_path(), error);
        fs::copy_file(descriptors / relative, index / relative, fs::copy_options::overwrite_existing, error);
        if (error) return failed("index", std::format("{}: {}", relative, error.message()));
    }
    const std::string index_path = mcpplibs::os::fs::utf8(index);
    for (const std::vector<std::string>& args : {std::vector<std::string>{"-C", index_path, "init", "-q"},
                                                 std::vector<std::string>{"-C", index_path, "add", "-A"},
                                                 std::vector<std::string>{"-C", index_path, "-c", "user.name=omui-devtools", "-c",
                                                                          "user.email=omui-devtools@localhost", "-c", "commit.gpgsign=false",
                                                                          "commit", "-q", "-m", "mcpp-index with this tree's descriptors (release try)"}})
        if (auto git = run::output(root, "git", args); !git) return failed("index", git.error());
    passed("index", since, {{"packages", closure["packages"].size()}});

    // A clean home: its own store, toolchains and index (MCPP_HOME), the stand-in as its default index.
    since = clock::now();
    const fs::path home = work / "home";
    const std::vector<std::pair<std::string, std::string>> in_home{{"MCPP_HOME", mcpplibs::os::fs::utf8(home)},
                                                                   {"MCPP_JOBS", std::to_string(run::share_of_machine())}};
    const auto mcpp = [&](std::vector<std::string> args, const fs::path& cwd, std::vector<std::pair<std::string, std::string>> extra = {}) {
        run::How how{.cwd = cwd, .merged = true, .variables = in_home};
        how.variables.insert(how.variables.end(), extra.begin(), extra.end());
        return run::program(root, "mcpp", std::move(args), how);
    };
    const auto ok = [](const std::expected<run::Ran, std::string>& ran) { return ran && ran->status == 0; };
    const auto said = [](const std::expected<run::Ran, std::string>& ran) { return ran ? ran->out : ran.error(); };
    if (auto made = mcpp({"self", "init"}, work); !ok(made)) return failed("home", said(made));
    const std::string config = release::with_default_index(mcpplibs::os::fs::read(home / "config.toml").value_or(""), mcpplibs::os::fs::generic_utf8(index));
    if (config.empty()) return failed("home", "the new home's config.toml has no [index.repos.\"mcpplibs\"]");
    std::ofstream(home / "config.toml", std::ios::binary) << config;
    if (auto updated = mcpp({"index", "update"}, work); !ok(updated)) return failed("home", said(updated));
    // Each archive where the home's store keeps the payload of that version.
    std::map<std::string, std::string> versions;
    for (const auto& package : closure["packages"]) versions[package.value("path", "")] = package.value("version", "");
    for (const auto& package : closure["packages"]) {
        const std::string text = mcpplibs::os::fs::read(descriptors / package.value("descriptor", "")).value_or("");
        const auto [namespace_, name] = release::identity_of(text);
        if (name.empty()) return failed("home", std::format("{}: its descriptor states no name", package.value("name", "")));
        const fs::path into = home / release::store_path(namespace_, name, package.value("version", ""));
        fs::create_directories(into, error);
        const fs::path archive = dist / package.value("archive", "");
        if (auto unpacked = run::output(root, "tar", {"-xzf", mcpplibs::os::fs::utf8(archive), "-C", mcpplibs::os::fs::utf8(into)}); !unpacked)
            return failed("home", std::format("{}: {}", mcpplibs::os::fs::utf8(archive.filename()), unpacked.error()));
        std::ofstream(into / ".xpkg.lua", std::ios::binary) << text;
        // What publish left as paths, pinned to the members' versions (mcpp#962; the stand-in's alone).
        const fs::path manifest = into / mcpplibs::os::fs::from_utf8(std::format("{}-{}", package.value("name", ""), package.value("version", ""))) / "mcpp.toml";
        auto made_pinned = release::pinned(mcpplibs::os::fs::read(manifest).value_or(""), package.value("path", ""), versions);
        if (!made_pinned) return failed("home", std::format("{}: {}", mcpplibs::os::fs::utf8(manifest), made_pinned.error()));
        std::ofstream(manifest, std::ios::binary) << *made_pinned;
    }
    passed("home", since);

    // With the omui tool (E2): `omui new`, then the first window through the player it ships — nothing compiled;
    // new to first window within a minute on this home (the downloads are the seeded store's).
    if (const auto tool_text = options.value("tool")) {
        // The tool installed as a user installs it (red 142): through xlings from its descriptor (omui.lua), in an xlings
        // home of its own, onto xlings' C library and graphics stack; the host's (glibc 2.39 here) is never used. Its
        // archive seeded where xlings downloads to (nothing is published); its dependencies from this machine's store.
        const fs::path released = fs::absolute(mcpplibs::os::fs::from_utf8(*tool_text));
        fs::path archive;
        for (const auto& entry : fs::directory_iterator(released, error))
            if (const std::string name = mcpplibs::os::fs::utf8(entry.path().filename()); name.starts_with("omui-") && name.ends_with(".tar.gz")) archive = entry.path();
        if (archive.empty() || !fs::exists(released / "omui.lua", error))
            return failed("tool", std::format("no omui.lua and omui-*.tar.gz in {} (omui-devtools release tool)", *tool_text));
        since = clock::now();
        // Outside any project: inside the checkout, xlings would take the checkout's .xlings.json as the scope of what
        // the home installs and its shims run.
        const fs::path xlings_home = fs::temp_directory_path() / "omui-release-xlings";
        const auto in_xlings = release::xlings_home(root, xlings_home, released / "omui.lua");
        if (!in_xlings) return failed("tool: install", in_xlings.error());
        fs::create_directories(xlings_home / "data/runtimedir", error);
        fs::remove(xlings_home / "data/runtimedir" / archive.filename(), error);
        fs::copy_file(archive, xlings_home / "data/runtimedir" / archive.filename(), error);
        const auto xlings = [&](std::vector<std::string> args) {
            return run::program(root, mcpplibs::os::fs::utf8(xlings_home / "bin/xlings"), std::move(args),
                                {.cwd = xlings_home, .merged = true, .variables = in_xlings->variables, .unset = in_xlings->unset, .timeout = std::chrono::seconds(900)});
        };
        (void)xlings({"remove", "omui", "-y", "--global"});
        if (auto installed = xlings({"install", "omui", "-y", "--global"}); !ok(installed)) return failed("tool: install", said(installed));
        // Its programs on PATH as xvm puts them, and where the package went.
        const fs::path shims = in_xlings->shims;
        fs::path installed;
        for (const auto& package : fs::directory_iterator(xlings_home / "data/xpkgs", error))
            if (mcpplibs::os::fs::utf8(package.path().filename()).ends_with("-x-omui"))
                for (const auto& version : fs::directory_iterator(package.path(), error)) installed = version.path();
        if (!fs::exists(shims / "omui", error) || !fs::exists(shims / "omui-player", error) || installed.empty())
            return failed("tool: install", std::format("no omui and omui-player shims in {}, or no package under its xpkgs", mcpplibs::os::fs::utf8(shims)));
        passed("tool: install", since, {{"package_bytes", bytes_under(installed)}, {"archive_bytes", fs::file_size(archive, error)}});
        // The project outside any checkout (fonts, the source tree's fallbacks: none of them here).
        const fs::path scratch = fs::temp_directory_path() / "omui-release-try-tool";
        fs::remove_all(scratch, error);
        fs::create_directories(scratch, error);
        std::vector<std::pair<std::string, std::string>> with_tool = in_home;
        with_tool.insert(with_tool.end(), in_xlings->variables.begin(), in_xlings->variables.end());
        with_tool.emplace_back("PATH", std::format("{}:{}", mcpplibs::os::fs::utf8(shims), mcpplibs::os::env::get("PATH").value_or("/usr/bin:/bin")));
        const auto omui = [&](std::vector<std::string> args, const fs::path& cwd, std::vector<std::pair<std::string, std::string>> extra = {}) {
            run::How how{.cwd = cwd, .merged = true, .variables = with_tool, .unset = in_xlings->unset};
            how.variables.insert(how.variables.end(), extra.begin(), extra.end());
            return run::program(root, mcpplibs::os::fs::utf8(shims / "omui"), std::move(args), how);
        };
        since = clock::now();
        if (auto made = omui({"new", "hello", "--template", template_name}, scratch); !ok(made)) return failed("tool: new", said(made));
        const fs::path picture = scratch / "first.png";
        if (auto shown = omui({"run", "--", "--frames", "3", "--png", mcpplibs::os::fs::utf8(picture)}, scratch / "hello", {{"SDL_VIDEO_DRIVER", "offscreen"}});
            !ok(shown))
            return failed("tool: first window", said(shown));
        const double seconds = std::chrono::duration<double>(clock::now() - since).count();
        const auto dark = release::ink(picture);
        if (!dark) return failed("tool: first window", dark.error());
        fs::copy_file(picture, work / "first.png", fs::copy_options::overwrite_existing, error);
        if (fs::exists(scratch / "hello/target", error))
            return failed("tool: first window", "the project was built (hello/target is there): the player the tool ships was not used");
        // Its words drawn: dark pixels of the title and the count (no fonts found draws none).
        const std::size_t ink = dark->dark;
        if (ink < 200) return failed("tool: first window", std::format("{} dark pixels: its text was not drawn (the fonts the tool carries not found)", ink));
        if (seconds > 60) return failed("tool: first window", std::format("{:.1f} s from new to the first window: over the minute E2 allows", seconds));
        passed("tool: new to first window", since, {{"width", dark->width}, {"height", dark->height}, {"compiled", false}, {"ink_pixels", ink}, {"limit_seconds", 60}});
        fs::remove_all(scratch, error);
        report["ok"] = true;
        written();
        std::println("release try (tool): ok — {}", mcpplibs::os::fs::utf8(work / "try.json"));
        return 0;
    }

    // A project from the template, as `omui new` makes one (mcpp new --template omui.sdk:…).
    since = clock::now();
    const std::string project_name = "hello";
    if (auto made = mcpp({"new", project_name, "--template", std::format("{}:{}", release::template_package, template_name)}, work); !ok(made))
        return failed("new", said(made));
    const fs::path project = work / project_name;
    // Every template's project, made the same way: its manifest one that a strict TOML reader takes (the player reads
    // its own; mcpp's parser takes more, mcpp#964).
    std::string unreadable;
    for (const std::string_view other : release::template_names) {
        const fs::path where = work / "new" / std::string(other);
        fs::create_directories(where, error);
        const std::string name = std::format("t_{}", other.substr(0, other.find('-')));
        if (auto made = mcpp({"new", name, "--template", std::format("{}:{}", release::template_package, other)}, where); !ok(made))
            return failed("new", std::format("{}: {}", other, said(made)));
        if (const auto problem = release::toml_problem(mcpplibs::os::fs::read(where / name / "mcpp.toml").value_or("")); !problem.empty())
            unreadable += std::format("{}: mcpp.toml:{}\n", other, problem);
    }
    if (!unreadable.empty()) return failed("new", unreadable);
    passed("new", since, {{"templates", release::template_names.size()}});

    // Built: everything it needs from the index, the toolchain too, into the clean home.
    since = clock::now();
    if (auto built = mcpp({"build"}, project); !ok(built)) return failed("build", said(built));
    passed("build", since, {{"home_bytes", bytes_under(home)}, {"target_bytes", bytes_under(project / "target")}});

    // Its first window, drawn and read back (SDL's offscreen driver: the window path without a display).
    since = clock::now();
    const fs::path picture = work / "first.png";
    if (auto shown = mcpp({"run", "--", "--frames", "3", "--png", mcpplibs::os::fs::utf8(picture)}, project, {{"SDL_VIDEO_DRIVER", "offscreen"}});
        !ok(shown))
        return failed("first window", said(shown));
    auto image = mlibs::image::load_png(picture);
    if (!image) return failed("first window", std::format("{}: {}", mcpplibs::os::fs::utf8(picture), image.error()));
    std::set<std::uint32_t> colours;
    for (int y = 0; y < image->height && colours.size() < 64; y += 4) {
        const auto line = image->row(y);
        for (std::size_t x = 0; x + 4 <= line.size() && colours.size() < 64; x += 16)
            colours.insert(static_cast<std::uint32_t>(line[x]) << 24 | static_cast<std::uint32_t>(line[x + 1]) << 16 | static_cast<std::uint32_t>(line[x + 2]) << 8 | line[x + 3]);
    }
    if (colours.size() < 2) return failed("first window", "the first frame is one colour: nothing was drawn");
    passed("first window", since, {{"width", image->width}, {"height", image->height}, {"colours_sampled", colours.size()}});
    report["ok"] = true;
    written();
    std::println("release try: ok — {}", mcpplibs::os::fs::utf8(work / "try.json"));
    return 0;
}

int platforms(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto table = mcpplibs::os::fs::read(root / "docs/release.platforms.xdef.json");
    if (!table) {
        std::cerr << "omui-devtools platforms: cannot read docs/release.platforms.xdef.json\n";
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
        if (mcpplibs::os::fs::read(target) == *written) continue;
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

int docs(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto inventory = omui::devtools::docs::read(root);
    if (!inventory) {
        std::cerr << "omui-devtools docs: " << inventory.error() << '\n';
        return 2;
    }
    const auto reference = omui::devtools::docs::reference_of(root);
    if (!reference) {
        std::cerr << "omui-devtools docs: " << reference.error() << '\n';
        return 2;
    }
    int stale = 0;
    for (const std::string_view language : {"en", "zh"})
        for (const std::string_view page : {"maturity", "experimental", "reference"}) {
            const std::string written = page == "maturity"     ? omui::devtools::docs::maturity_page(*inventory, language)
                                        : page == "experimental" ? omui::devtools::docs::experimental_page(*inventory, language)
                                                                 : omui::devtools::docs::reference_page(*reference, language);
            const std::string where = std::format("docs/{}/{}.md", language, page);
            const fs::path target = root / mcpplibs::os::fs::from_utf8(where);
            if (mcpplibs::os::fs::read(target) == written) continue;
            if (options.is_flag_set("check")) {
                std::cerr << std::format("omui-devtools docs: {} is not current (omui-devtools docs)\n", where);
                ++stale;
                continue;
            }
            std::ofstream(target, std::ios::binary) << written;
            std::cout << std::format("wrote {}\n", where);
        }
    return stale ? 1 : 0;
}

int closure(const cmd::ParsedArgs& options, const fs::path& root) {
    const auto report = omui::devtools::closure::check(root);
    if (!report) {
        std::cerr << "omui-devtools closure: " << report.error() << '\n';
        return 2;
    }
    int stale = 0;
    for (const std::string_view language : {"en", "zh"}) {
        const std::string written = omui::devtools::closure::page(*report, language);
        const std::string where = std::format("docs/{}/closure.md", language);
        const fs::path target = root / mcpplibs::os::fs::from_utf8(where);
        if (mcpplibs::os::fs::read(target) == written) continue;
        if (options.is_flag_set("check")) {
            std::cerr << std::format("omui-devtools closure: {} is not current (omui-devtools closure)\n", where);
            ++stale;
            continue;
        }
        std::ofstream(target, std::ios::binary) << written;
        std::cout << std::format("wrote {}\n", where);
    }
    std::cout << std::format("closure: {} passed, {} owed\n", report->passed(), report->owed());
    if (options.is_flag_set("gate") && report->owed() > 0) return 1;
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
    if (parsed->subcommand_name() == "docs") return docs(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "closure") return closure(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "rename") {
        const cmd::ParsedArgs& asked = parsed->subcommand()->get();
        const std::string from(asked.positional(0)), to(asked.positional(1));
        auto planned = omui::devtools::rename::plan(root, from, to);
        if (!planned) {
            std::cerr << "omui-devtools rename: " << planned.error() << '\n';
            return 2;
        }
        std::size_t words = 0;
        for (const auto& [path, count] : planned->edits) {
            std::println("{:>5}  {}", count, path);
            words += count;
        }
        for (const auto& [path, moved] : planned->moves) std::println("moves  {} → {}", path, moved);
        std::println("{} → {}: {} words in {} files, {} paths moved{}", from, to, words, planned->edits.size(), planned->moves.size(), asked.is_flag_set("write") ? "" : " (nothing written: --write)");
        if (!asked.is_flag_set("write")) return 0;
        if (auto applied = omui::devtools::rename::apply(root, *planned, from, to); !applied) {
            std::cerr << "omui-devtools rename: " << applied.error() << '\n';
            return 1;
        }
        std::println("written; next: omui-devtools lint, then build what moved");
        return 0;
    }
    if (parsed->subcommand_name() == "split") {
        const cmd::ParsedArgs& asked = parsed->subcommand()->get();
        const std::string project(asked.positional(0));
        auto planned = omui::devtools::split::plan(root, project);
        if (!planned) {
            std::cerr << "omui-devtools split: " << planned.error() << '\n';
            return 2;
        }
        for (const auto& member : planned->members) std::println("  {}", member);
        std::println("{}: {} members, {} files", project, planned->members.size(), planned->files.size());
        if (asked.is_flag_set("list")) return 0;
        const auto leaf = mcpplibs::os::fs::utf8(mcpplibs::os::fs::from_utf8(project).filename());
        const fs::path out = asked.value("out") ? mcpplibs::os::fs::from_utf8(*asked.value("out")) : root / "target" / "split" / mcpplibs::os::fs::from_utf8(leaf);
        if (auto written = omui::devtools::split::write(root, *planned, out); !written) {
            std::cerr << "omui-devtools split: " << written.error() << '\n';
            return 1;
        }
        std::println("written to {}; next: mcpp test --workspace there", mcpplibs::os::fs::utf8(out));
        return 0;
    }
    if (parsed->subcommand_name() == "release") {
        const cmd::ParsedArgs& asked = parsed->subcommand()->get();
        if (asked.subcommand_name() == "try") return release_try(asked.subcommand()->get(), root);
        if (asked.subcommand_name() == "tool") return release_tool(asked.subcommand()->get(), root);
        if (asked.subcommand_name() == "runtime") {
            const auto& given = asked.subcommand()->get();
            return omui::devtools::release::runtime(root, {.out = mcpplibs::os::fs::from_utf8(given.value("out").value_or("")),
                                           .version = given.value("version").value_or(""),
                                           .pack = !given.is_flag_set("no-pack"),
                                           .check = given.is_flag_set("check"),
                                           .home = mcpplibs::os::fs::from_utf8(given.value("home").value_or("")),
                                           .omui = mcpplibs::os::fs::from_utf8(given.value("omui").value_or(""))});
        }
        if (asked.subcommand_name() != "index") {
            std::cerr << "usage: omui-devtools release index [--out DIR] [--list] [--tag T] [--index DIR] | try [--template T] [--work DIR] [--tool DIR] | tool [--out DIR]\n";
            return 2;
        }
        return release_index(asked.subcommand()->get(), root);
    }
    if (parsed->subcommand_name() == "api") return surface(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "eval") return omui::devtools::eval::run(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "consumer") {
        const auto& given = parsed->subcommand()->get();
        omui::devtools::consumer::Options options;
        if (const auto output = given.value("output")) options.output = fs::absolute(mcpplibs::os::fs::from_utf8(*output));
        options.keep_project = given.is_flag_set("keep-project");
        options.web = given.is_flag_set("web");
        if (const auto openas = given.value("openas-package")) options.openas = fs::absolute(mcpplibs::os::fs::from_utf8(*openas));
        return omui::devtools::consumer::probe(root, given.positional(0), options);
    }
    if (parsed->subcommand_name() == "lint") return lint(parsed->subcommand()->get(), root);
    if (parsed->subcommand_name() == "browser") {
        namespace page = omui::devtools::browser;
        const auto& given = parsed->subcommand()->get();
        page::Options options;
        if (const auto chrome = given.value("chrome")) options.chrome = mcpplibs::os::fs::from_utf8(*chrome);
        if (const auto seconds = given.value("timeout")) {
            int count = 0;
            const auto [end, failed] = std::from_chars(seconds->data(), seconds->data() + seconds->size(), count);
            if (failed != std::errc{} || end != seconds->data() + seconds->size() || count < 1) {
                std::cerr << "omui-devtools browser: --timeout takes seconds, at least 1\n";
                return 2;
            }
            options.timeout = std::chrono::seconds(count);
        }
        options.flags = given.option_or_empty("flag").values;
        for (const auto& preload : given.option_or_empty("preload").values) {
            const auto colon = preload.rfind(':');
            if (colon == std::string::npos) {
                std::cerr << "omui-devtools browser: --preload takes DIR:/DEST\n";
                return 2;
            }
            options.preloads.emplace_back(fs::absolute(mcpplibs::os::fs::from_utf8(preload.substr(0, colon))), preload.substr(colon + 1));
        }
        options.headed = given.is_flag_set("headed") || mcpplibs::os::env::get("OMUI_BROWSER_HEADED") == "1";
        options.program = mcpplibs::os::fs::from_utf8(given.positional(0));
        for (std::size_t index = 1; index < given.positional_count(); ++index) options.arguments.push_back(given.positional(index));
        const auto status = page::run(options);
        if (!status) {
            std::cerr << "omui-devtools browser: " << status.error() << '\n';
            return 2;
        }
        return *status;
    }
    if (parsed->subcommand_name() == "media") {
        const auto& given = parsed->subcommand()->get();
        std::vector<std::string> shows;
        for (std::size_t index = 0; index < given.positional_count(); ++index) shows.push_back(given.positional(index));
        if (given.is_flag_set("showcases")) {
            std::error_code error;
            for (const auto& entry : fs::directory_iterator(root / "showcase", error))
                if (fs::is_regular_file(entry.path() / "mcpp.toml")) shows.push_back(mcpplibs::os::fs::utf8(entry.path().filename()));
            std::ranges::sort(shows);
        }
        return omui::devtools::check::media(root, std::move(shows), given.is_flag_set("check"));
    }
    if (parsed->subcommand_name() == "check") {
        namespace step = omui::devtools::check;
        const auto& sub = parsed->subcommand()->get();
        if (!sub.has_subcommand()) {
            std::cerr << "usage: omui-devtools check features | templates | reflect [--profile P] | sweep [--profile P] | tools-e2e | agent-e2e | distribution [--profile P] | android [--out DIR] | web [--profile P] | browser | site [CHECK…] | visitor-path | live-page [--page DIR] | site-page [PART…] [--page DIR] [--omui PATH] | dist-toy-loading | toy [PACKAGE…] | size -p MEMBER [--json F] [--markdown F] [--budget F] | atspi [--profile P]\n";
            return 2;
        }
        const auto& given = sub.subcommand()->get();
        if (sub.subcommand_name() == "features") return step::features(root);
        if (sub.subcommand_name() == "templates") return step::templates(root);
        if (sub.subcommand_name() == "browser") return step::browser_checks(root);
        if (sub.subcommand_name() == "visitor-path")
            return step::visitor_path(root, mcpplibs::os::fs::from_utf8(given.value("site").value_or("")), mcpplibs::os::fs::from_utf8(given.value("omui").value_or("")),
                                      given.value("url").value_or(""), given.is_flag_set("allow-local"));
        if (sub.subcommand_name() == "live-page") return step::live_page(root, mcpplibs::os::fs::from_utf8(given.value("page").value_or("")));
        if (sub.subcommand_name() == "site-page") {
            std::vector<std::string> parts;
            for (std::size_t index = 0; index < given.positional_count(); ++index) parts.push_back(given.positional(index));
            return step::site_page(root, mcpplibs::os::fs::from_utf8(given.value("page").value_or("")), mcpplibs::os::fs::from_utf8(given.value("omui").value_or("")),
                                   std::move(parts));
        }
        if (sub.subcommand_name() == "dist-toy-loading") return step::dist_toy_loading(root, mcpplibs::os::fs::from_utf8(given.value("fixture").value_or("")));
        if (sub.subcommand_name() == "site") {
            std::vector<std::string> names;
            for (std::size_t index = 0; index < given.positional_count(); ++index) names.push_back(given.positional(index));
            return step::site(root, mcpplibs::os::fs::from_utf8(given.value("site").value_or("")), std::move(names));
        }
        if (sub.subcommand_name() == "web") return step::web(root, given.value("profile").value_or("wasm-test"));
        if (sub.subcommand_name() == "reflect") return step::reflect(root, given.value("profile").value_or("dev"));
        if (sub.subcommand_name() == "size") {
            if (!given.value("package")) {
                std::cerr << "omui-devtools check size: -p MEMBER\n";
                return 2;
            }
            step::Size asked;
            asked.package = *given.value("package");
            asked.json = mcpplibs::os::fs::from_utf8(given.value("json").value_or(""));
            asked.markdown = mcpplibs::os::fs::from_utf8(given.value("markdown").value_or(""));
            asked.native = given.is_flag_set("native");
            asked.budget = mcpplibs::os::fs::from_utf8(given.value("budget").value_or(""));
            return step::size(root, asked);
        }
        std::vector<std::string> profile;
        if (const auto chosen = given.value("profile")) profile = {"--profile", *chosen};
        if (sub.subcommand_name() == "sweep") return step::sweep(root, profile);
        if (sub.subcommand_name() == "distribution") return step::distribution(root, profile);
        if (sub.subcommand_name() == "android")
            return step::android(root, given.value("out") ? mcpplibs::os::fs::from_utf8(*given.value("out")) : fs::temp_directory_path() / "omui-android");
        if (sub.subcommand_name() == "tools-e2e") return step::tools_e2e(root, profile);
        if (sub.subcommand_name() == "agent-e2e") return step::agent_e2e(root, profile);
        if (sub.subcommand_name() == "toy") {
            std::vector<std::string> packages;
            for (std::size_t index = 0; index < given.positional_count(); ++index) packages.push_back(given.positional(index));
            return step::toy(root, std::move(packages));
        }
        return step::atspi(root, profile);
    }
    if (parsed->subcommand_name() == "site") return site_command(parsed->subcommand()->get(), root);
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
        if (sub.subcommand_name() == "material3") {
            const auto status = omui::devtools::bench::material3(root, mcpplibs::os::fs::from_utf8(sub.subcommand()->get().positional(0)));
            if (!status) {
                std::cerr << "omui-devtools bench material3: " << status.error() << '\n';
                return 2;
            }
            return *status;
        }
        if (sub.subcommand_name() == "apps") return omui::devtools::bench::apps(root, sub.subcommand()->get());
        if (sub.subcommand_name() != "competitors") {
            std::cerr << "usage: omui-devtools bench competitors [--ledger [--rounds N] [--dry-run]] | material3 OUT\n";
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
    std::cerr << "usage: omui-devtools [--root DIR] ci … | llvm … | platforms | docs | govern … | incremental … | api … | atspi … | lint … | lead … | bench … | gpu … | check … | media … | browser … | consumer …  (omui-devtools --help)\n";
    return 2;
}
