// eval's command line: its declaration and how a parsed line becomes a call (the program composes
// it, D-61).
module omui.devtools.eval;

import std;
import mcpplibs.cmdline;
import mcpplibs.os;

namespace omui::devtools::eval {

namespace cmd = mcpplibs::cmdline;
namespace fs = std::filesystem;
using cmd::Arg;
using cmd::Option;

cmd::App command() {
    return cmd::App("eval")
        .description("Evaluations that use what they judge: a real agent in an empty directory, "
                     "the outcome checked on the subject's own state")
        .subcommand(
            cmd::App("compare")
                .description("Side-by-side reviews (D-135, compare-v1): ahead, par or behind "
                             "against a similar project")
                .subcommand(cmd::App("sheets")
                                .description("Each pair as one blinded sheet (A and B; which is "
                                             "ours seeded; the key under target/compare/SUBJECT)")
                                .arg(Arg("subject").required().help(
                                    "the comparison set "
                                    "(.agents/docs/evaluation/compare/SUBJECT.compare.xdef.json)")))
                .subcommand(cmd::App("judge")
                                .description("Each sheet judged by fresh sessions with nothing of "
                                             "the project; unblinded; a dimension its weakest pair")
                                .arg(Arg("subject").required().help("the comparison set"))
                                .option(Option("agent").takes_value().value_name("NAME").help(
                                    "the judge (default: claude)"))
                                .option(Option("samples").takes_value().value_name("N").help(
                                    "sessions a sheet, the majority deciding (default: 3)"))
                                .option(Option("dry-run").help("print the records, append none")))
                .subcommand(
                    cmd::App("record")
                        .description("The maintainer's verdict, which decides")
                        .arg(Arg("subject").required().help("the comparison set"))
                        .arg(Arg("dimension").required().help("the dimension"))
                        .arg(Arg("verdict").required().help("behind, par or ahead"))
                        .option(Option("by").takes_value().value_name("WHO").help(
                            "the evaluator (default: maintainer)"))
                        .option(Option("note").takes_value().value_name("TEXT").help("why"))))
        .subcommand(
            cmd::App("tasks")
                .description("Agent task sets (§6.3): a goal in words, an agent with one way's "
                             "tools, the outcome checked on the application's state")
                .subcommand(
                    cmd::App("run")
                        .description("Each task on a fresh instance; a line of "
                                     "tasks.eval.xdef.jsonl a trial")
                        .arg(Arg("set").required().help("tools/eval/tasks/SET.tasks.xdef.json"))
                        .option(Option("only").takes_value().value_name("ID,…").help("these tasks"))
                        .option(Option("via").takes_value().value_name("WAY").help(
                            "semantic (default), a11y-tree or screenshot"))
                        .option(Option("agent").takes_value().value_name("NAME").help(
                            "default: claude"))
                        .option(Option("trials").takes_value().value_name("N").help(
                            "each task N times (default: 1)"))
                        .option(Option("dry-run").help("print the records, append none")))
                .subcommand(cmd::App("summary")
                                .description("Each way's latest trial of every task, side by side")
                                .arg(Arg("set").required().help("the set"))
                                .option(Option("split-only")
                                            .help("only trials whose tokens were recorded split "
                                                  "(fresh, cache reads apart)"))))
        .subcommand(
            cmd::App("animate")
                .description("A real agent animates each brief without a GUI (§10.3); the document "
                             "graded by hanim's own samples")
                .option(Option("only").takes_value().value_name("ID,…").help(
                    "these briefs (tools/eval/animation.briefs.xdef.json)"))
                .option(Option("trials").takes_value().value_name("N").help(
                    "each brief N times (default: 1)"))
                .option(Option("parallel")
                            .takes_value()
                            .value_name("N")
                            .help("trials at once (default: 3)"))
                .option(Option("agent").takes_value().value_name("NAME").help(
                    "claude (default) or codex"))
                .option(Option("model").takes_value().value_name("NAME").help(
                    "the agent's model (claude: sonnet)"))
                .option(Option("effort").takes_value().value_name("LEVEL").help(
                    "codex's reasoning effort (default: medium)"))
                .option(Option("timeout").takes_value().value_name("SECONDS").help(
                    "a trial at most (default: 1500)"))
                .option(Option("keep").takes_value().value_name("DIR").help(
                    "where trials stay (default: target/eval/animate)"))
                .option(
                    Option("grade").takes_value().value_name("DIR").help("grade one kept trial"))
                .option(Option("no-ledger").help("append nothing to animation.eval.xdef.jsonl")))
        .subcommand(cmd::App("duel")
                        .description("Two codex agents play the fighting game by turns; a third "
                                     "analyses the recording into a report")
                        .arg(Arg("p1").help("player 1's fighter (default: kata)"))
                        .arg(Arg("p2").help("player 2's fighter (default: mo)"))
                        .option(Option("turn").takes_value().value_name("FRAMES").help(
                            "a turn's frames (default: 10)"))
                        .option(Option("turns").takes_value().value_name("N").help(
                            "at most this many turns (default: 150)"))
                        .option(Option("timeout").takes_value().value_name("SECONDS").help(
                            "the players' time (default: 3600)"))
                        .option(Option("analyse").takes_value().value_name("DIR").help(
                            "analyse a recording made before (no play)"))
                        .option(Option("date")
                                    .takes_value()
                                    .value_name("YYYY-MM-DD")
                                    .help("the report's date (default: today)")))
        .subcommand(
            cmd::App("materials")
                .description("The material a comparison set compares")
                .subcommand(
                    cmd::App("controls")
                        .description(
                            "Ours from the site's quality lab beside Material 3's (@material/web), "
                            "four states each, under target/compare-material")))
        .subcommand(cmd::App("cold-start")
                        .description("A fresh agent with one program and one task in words (D-186, D-192); the outcome checked through the program")
                        .arg(Arg("set").required().help("tools/eval/coldstart/SET.coldstart.xdef.json"))
                        .option(Option("only").takes_value().value_name("ID,…").help("these tasks"))
                        .option(Option("agent").takes_value().value_name("NAME").help("default: claude"))
                        .option(Option("trials").takes_value().value_name("N").help("each task N times (default: 1)"))
                        .option(Option("keep").help("leave each trial's directory"))
                        .option(Option("dry-run").help("print the records, append none")))
        .subcommand(
            cmd::App("hot-reload")
                .description("Save to screen (W2 ≤ 100 ms at p95): a dev-mode application's script "
                             "saved again and again; until its tree shows the change")
                .option(Option("omui").takes_value().value_name("PATH").help("the omui command"))
                .option(Option("program").takes_value().value_name("PATH").help(
                    "the application's program"))
                .option(Option("app-dir").takes_value().value_name("DIR").help(
                    "its directory (mcpp.toml, logic/main.ts)"))
                .option(
                    Option("rounds").takes_value().value_name("N").help("saves (default: 20)")));
}

int run(const cmd::ParsedArgs& options, const fs::path& root) {
    if (options.subcommand_name() == "compare" && options.subcommand()) {
        const auto& compare = options.subcommand()->get();
        const auto& given = compare.subcommand() ? compare.subcommand()->get() : compare;
        if (compare.subcommand_name() == "sheets")
            return compare_sheets(root, given.positional(0));
        if (compare.subcommand_name() == "judge") {
            Judging judging;
            judging.agent = given.value("agent").value_or("claude");
            judging.dry_run = given.is_flag_set("dry-run");
            if (const auto samples = given.value("samples")) {
                const auto [end, failed] =
                    std::from_chars(samples->data(), samples->data() + samples->size(),
                                    judging.samples);
                if (failed != std::errc{} || end != samples->data() + samples->size() ||
                    judging.samples < 1) {
                    std::cerr << "omui-devtools eval compare judge: --samples takes a count of at "
                                 "least 1\n";
                    return 2;
                }
            }
            return compare_judge(root, given.positional(0), judging);
        }
        if (compare.subcommand_name() == "record")
            return compare_record(root, given.positional(0), given.positional(1),
                                  given.positional(2), given.value("by").value_or("maintainer"),
                                  given.value("note").value_or(""));
    }
    if (options.subcommand_name() == "tasks" && options.subcommand()) {
        const auto& tasks = options.subcommand()->get();
        if (!tasks.subcommand()) {
            std::cerr << "omui-devtools eval tasks: run SET or summary SET\n";
            return 2;
        }
        const auto& given = tasks.subcommand()->get();
        if (tasks.subcommand_name() == "summary")
            return tasks_summary(root, given.positional(0), given.is_flag_set("split-only"));
        Tasks run;
        if (const auto only = given.value("only"))
            for (const auto part : std::views::split(std::string_view(*only), ','))
                if (!part.empty())
                    run.only.emplace_back(part.begin(), part.end());
        run.via = given.value("via").value_or("semantic");
        run.agent = given.value("agent").value_or("claude");
        run.dry_run = given.is_flag_set("dry-run");
        if (const auto trials = given.value("trials")) {
            const auto [end, failed] =
                std::from_chars(trials->data(), trials->data() + trials->size(), run.trials);
            if (failed != std::errc{} || end != trials->data() + trials->size() || run.trials < 1) {
                std::cerr << "omui-devtools eval tasks: --trials takes a count of at least 1\n";
                return 2;
            }
        }
        return tasks_run(root, given.positional(0), run);
    }
    if (options.subcommand_name() == "animate" && options.subcommand()) {
        const auto& given = options.subcommand()->get();
        Animate animate;
        const auto count = [&](std::string_view name, int& value, int least) {
            const auto text = given.value(name);
            if (!text)
                return true;
            const auto [end, failed] =
                std::from_chars(text->data(), text->data() + text->size(), value);
            if (failed == std::errc{} && end == text->data() + text->size() && value >= least)
                return true;
            std::cerr
                << std::format("omui-devtools eval animate: --{} takes a count of at least {}\n",
                               name, least);
            return false;
        };
        int seconds = 1500;
        if (!count("trials", animate.trials, 1) || !count("parallel", animate.parallel, 1) ||
            !count("timeout", seconds, 1))
            return 2;
        animate.timeout = std::chrono::seconds(seconds);
        if (const auto only = given.value("only"))
            for (const auto part : std::views::split(std::string_view(*only), ','))
                if (!part.empty())
                    animate.only.emplace_back(part.begin(), part.end());
        animate.agent = given.value("agent").value_or("claude");
        if (animate.agent != "claude" && animate.agent != "codex") {
            std::cerr << "omui-devtools eval animate: --agent is claude or codex\n";
            return 2;
        }
        animate.model = given.value("model").value_or("");
        animate.effort = given.value("effort").value_or("medium");
        animate.keep = given.value("keep")
                           ? fs::absolute(mcpplibs::os::fs::from_utf8(*given.value("keep")))
                           : root / "target/eval/animate";
        if (const auto grade = given.value("grade"))
            animate.grade = fs::absolute(mcpplibs::os::fs::from_utf8(*grade));
        animate.no_ledger = given.is_flag_set("no-ledger");
        return eval::animate(root, animate);
    }
    if (options.subcommand_name() == "duel" && options.subcommand()) {
        const auto& given = options.subcommand()->get();
        Duel duel{.root = root};
        if (given.positional_count() > 0)
            duel.p1 = given.positional(0);
        if (given.positional_count() > 1)
            duel.p2 = given.positional(1);
        int seconds = 3600;
        for (auto [name, value] : {std::pair<std::string_view, int*>{"turn", &duel.frames},
                                   {"turns", &duel.turns},
                                   {"timeout", &seconds}}) {
            const auto text = given.value(name);
            if (!text)
                continue;
            const auto [end, failed] =
                std::from_chars(text->data(), text->data() + text->size(), *value);
            if (failed != std::errc{} || end != text->data() + text->size() || *value < 1) {
                std::cerr
                    << std::format("omui-devtools eval duel: --{} takes a count of at least 1\n",
                                   name);
                return 2;
            }
        }
        duel.timeout = std::chrono::seconds(seconds);
        if (const auto again = given.value("analyse"))
            duel.analyse = fs::absolute(mcpplibs::os::fs::from_utf8(*again));
        duel.date = given.value("date").value_or("");
        return eval::duel(duel);
    }
    if (options.subcommand_name() == "materials" && options.subcommand()) {
        if (options.subcommand()->get().subcommand_name() == "controls")
            return materials_controls(root);
        std::cerr << "omui-devtools eval materials: controls\n";
        return 2;
    }
    if (options.subcommand_name() == "cold-start" && options.subcommand()) {
        const auto& given = options.subcommand()->get();
        ColdStart cold;
        if (const auto only = given.value("only"))
            for (const auto part : std::views::split(std::string_view(*only), ','))
                if (!part.empty()) cold.only.emplace_back(part.begin(), part.end());
        cold.agent = given.value("agent").value_or("claude");
        cold.dry_run = given.is_flag_set("dry-run");
        cold.keep = given.is_flag_set("keep");
        if (const auto trials = given.value("trials")) {
            const auto [end, failed] = std::from_chars(trials->data(), trials->data() + trials->size(), cold.trials);
            if (failed != std::errc{} || end != trials->data() + trials->size() || cold.trials < 1) {
                std::cerr << "omui-devtools eval cold-start: --trials takes a count of at least 1\n";
                return 2;
            }
        }
        return cold_start(root, given.positional(0), cold);
    }
    if (options.subcommand_name() == "hot-reload" && options.subcommand()) {
        const auto& given = options.subcommand()->get();
        if (!given.value("omui") || !given.value("program") || !given.value("app-dir")) {
            std::cerr
                << "omui-devtools eval hot-reload: --omui, --program and --app-dir are needed\n";
            return 2;
        }
        HotReload hot;
        hot.omui = mcpplibs::os::fs::from_utf8(given.value("omui").value_or(""));
        hot.program =
            fs::absolute(mcpplibs::os::fs::from_utf8(given.value("program").value_or("")));
        hot.app_dir =
            fs::absolute(mcpplibs::os::fs::from_utf8(given.value("app-dir").value_or("")));
        if (const auto rounds = given.value("rounds")) {
            const auto [end, failed] =
                std::from_chars(rounds->data(), rounds->data() + rounds->size(), hot.rounds);
            if (failed != std::errc{} || end != rounds->data() + rounds->size() || hot.rounds < 1) {
                std::cerr
                    << "omui-devtools eval hot-reload: --rounds takes a count of at least 1\n";
                return 2;
            }
        }
        return hot_reload(hot);
    }
    std::cerr << "omui-devtools eval: compare, tasks, animate, duel, materials, hot-reload "
                 "(omui-devtools eval --help)\n";
    return 2;
}

}  // namespace omui::devtools::eval
