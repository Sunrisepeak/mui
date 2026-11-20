// CI's matrix on one Linux machine (moved from tools/ci/local.sh), for when hosted runners are unavailable: lint, GCC 16
// (dev and release), LLVM 23, the benchmarks' budgets, the acceptance steps, and the Windows target built with MinGW
// and run under wine. The pinned determinism hashes in the tests make a pass on every row the same cross-toolchain
// gate the hosted matrix is. Each row's result lines are shown; the status is 1 when a row failed.
module omui.devtools.check;

import std;
import mcpplibs.os;
import omui.devtools.run;
import :support;

namespace omui::devtools::check {

using namespace support;
namespace fs = std::filesystem;

int matrix(const fs::path& root, const std::vector<std::string>& members) {
    // omui.reflect is C++26 with a graph of its own: the GCC dev row tests it by itself, as CI does.
    std::vector<std::string> select{"--workspace", "--exclude", "omui.reflect"};
    if (!members.empty()) {
        select.clear();
        for (const auto& member : members) select.insert(select.end(), {"-p", member});
    }
    int failures = 0;
    const auto step = [](std::string_view name) { std::cout << std::format("\n== {}\n", name) << std::flush; };
    // A row: its result and failure lines shown, its status counted.
    const auto row = [&](std::vector<std::string> args, std::vector<std::pair<std::string, std::string>> variables = {}) {
        variables.emplace_back("MCPP_JOBS", std::to_string(run::share_of_machine()));
        const auto done = run::program(root, "mcpp", std::move(args), {.merged = true, .variables = std::move(variables)});
        for (auto part : std::views::split(std::string_view(done ? done->out : done.error()), '\n')) {
            const std::string_view line(part.begin(), part.end());
            if (line.contains("result") || line.contains("FAIL")) std::cout << line << '\n';
        }
        if (!done || done->status != 0) ++failures;
    };
    const auto with = [&](std::initializer_list<std::string_view> head, std::initializer_list<std::string_view> tail = {}) {
        std::vector<std::string> args(head.begin(), head.end());
        args.insert(args.end(), select.begin(), select.end());
        args.insert(args.end(), tail.begin(), tail.end());
        return args;
    };
    const auto self = mcpplibs::os::env::image();
    if (!self) return failed("this program's own image is unknown");
    const std::string devtools = mcpplibs::os::fs::utf8(*self);

    step("lint");
    if (const auto linted = run::program(root, devtools, {"--root", mcpplibs::os::fs::utf8(root), "lint"}, {.shown = true}); !linted || linted->status != 0) ++failures;
    if (const auto tested = run::program(root, devtools, {"--root", mcpplibs::os::fs::utf8(root), "lint", "--self-test"}); !tested || tested->status != 0) ++failures;
    step("gcc@16.1.0 dev");
    row(with({"test"}, {"--toolchain", "gcc@16.1.0"}));
    if (members.empty()) {
        step("omui.reflect (C++26, gcc@16.1.0 dev)");
        failures += reflect(root, "dev");
    }
    step("gcc@16.1.0 release");
    row(with({"test"}, {"--toolchain", "gcc@16.1.0", "--profile", "release"}));
    step("llvm@23.1.3 dev");
    row(with({"test"}, {"--toolchain", "llvm@23.1.3"}));
    step("benchmarks against their budgets (release)");
    row({"test", "-p", "omui.bench", "--profile", "release", "budgets"}, {{"OMUI_BENCH", "1"}});
    step("feature selection (M0 ⑤)");
    failures += features(root);
    step("templates (M0 ①)");
    failures += templates(root);
    step("agent e2e (example 07 in its own process)");
    failures += agent_e2e(root, {});
    if (mcpplibs::os::env::find_executable("wine")) {
        step("x86_64-windows-gnu under wine");
        std::vector<std::string> args{"--target", "x86_64-windows-gnu"};
        args.insert(args.end(), select.begin(), select.end());
        const auto programs = run::tests(root, args);
        if (!programs) {
            std::cout << "::error::" << programs.error() << '\n';
            ++failures;
        } else {
            for (const auto& [name, program] : *programs) {
                const auto ran = run::program(root, "timeout", {"300", "wine", mcpplibs::os::fs::utf8(program)}, {.merged = true, .variables = {{"WINEDEBUG", "-all"}}});
                if (ran && ran->status == 0) {
                    std::cout << std::format("  ok    {}\n", mcpplibs::os::fs::utf8(program.filename()));
                } else {
                    std::cout << std::format("  FAIL  {}\n{}", mcpplibs::os::fs::utf8(program.filename()), ran ? ran->out : ran.error());
                    ++failures;
                }
            }
        }
    }
    std::cout << (failures == 0 ? "\nmatrix: every row passed\n" : std::format("\nmatrix: {} row(s) failed\n", failures));
    return failures == 0 ? 0 : 1;
}

}  // namespace omui::devtools::check
