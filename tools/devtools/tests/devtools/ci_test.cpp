// omui-devtools ci: run 43685505783's failed rows (a cut of `gh run view
// --log-failed`) read as they were read by hand — the members, programs,
// cases, timeouts and the package that did not build — and set against a red list.
import std;
import mcpplibs.mtest;
import omui.devtools.ci;

using namespace mcpplibs::mtest;
namespace ci = omui::devtools::ci;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path(); }

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

const ci::Row* row_named(const std::vector<ci::Row>& rows, std::string_view name) {
    auto found = std::ranges::find(rows, name, &ci::Row::name);
    return found == rows.end() ? nullptr : &*found;
}

std::vector<std::string> paths(const ci::Row& row) {
    std::vector<std::string> list;
    for (const auto& member : row.members) list.push_back(member.path);
    return list;
}

constexpr std::string_view red_list = R"(# Status

## CI red list

| # | Failure | Where | Since | Kind | State |
|---|---|---|---|---|---|
| 55 | macOS: `omui.host.sdl3`'s WKWebView case and the sdk's `present_test` | macOS | — | E | open |
| 96 | `meng.nav`'s pinned crowd is other bits | Web, Windows | — | R | root cause: Detour's cosf |
| 82 | Windows: `tests/bench` did not build, so its tests did not run | Windows | — | E | upstream |
| 97 | `mrender.mesh`'s ball under light killed at 300 s | Linux x64 | — | F | open |

## Next
)";

}  // namespace

static UTest cases{"devtools.ci", [] {

    "a run's failed rows: each member, its programs, cases and first lines, as read by hand"_test = [] {
        const auto rows = ci::read_log(read(here() / "data" / "run-43685505783.failed.log"));
        expect(rows.size() == 4u) << rows.size();

        const auto* web = row_named(rows, "test web wasm32-emscripten");
        expect(web && paths(*web) == std::vector<std::string>{"modules/projects/meng/game/nav/cts"});
        if (web) {
            const auto& failure = web->members[0].failures.at(0);
            expect(failure.program == "nav_test" && failure.why == "exit 1, 0.96s, 13 cases") << failure.program << failure.why;
            expect(failure.cases.size() == 1u && failure.cases[0].starts_with("nav/a crowd walks through the doorway")) << failure.cases.size();
            expect(web->summary.starts_with("workspace test: 1/83 member(s) failed")) << web->summary;
        }

        const auto* mac = row_named(rows, "test macos-15 llvm@23.1.3 ci");
        expect(mac && paths(*mac) == std::vector<std::string>{"modules/projects/omui/sdk", "modules/projects/omui/host/sdl3", "showcase/quay/play"});
        if (mac && mac->members.size() == 3) {
            const auto& timeout = mac->members[0].failures.at(0);
            expect(timeout.program == "present_test" && timeout.why == "timeout after 300s") << timeout.why;
            expect(timeout.cases.empty() && timeout.last_passed.starts_with("present/")) << timeout.last_passed;
            const auto& quay = mac->members[2].failures.at(0);
            expect(quay.cases.size() == 1u && quay.cases[0].starts_with("quay.desktop/the harbour in a window"));
            expect(std::ranges::any_of(quay.head, [](const std::string& line) { return line.contains("No supported SDL_GPU backend found!"); }))
                << "the check's message is among its first lines";
        }

        const auto* windows = row_named(rows, "test windows-2022 llvm@23.1.3 ci");
        expect(windows && paths(*windows) == std::vector<std::string>{"modules/projects/meng/game/nav/cts", "tests/bench", "showcase/quay/picture"});
        if (windows && windows->members.size() == 3) {
            const auto& unbuilt = windows->members[1].failures.at(0);
            expect(unbuilt.program.empty() && unbuilt.why == "did not build");
            expect(!unbuilt.head.empty() && unbuilt.head[0].starts_with("build failed in gpu")) << "its head is the build that failed";
            expect(std::ranges::any_of(windows->build_errors, [](const std::string& line) { return line.starts_with("build failed in gpu v0.1.0"); }));
            expect(std::ranges::any_of(windows->build_errors, [](const std::string& line) { return line.contains("user-mapped section"); }))
                << "the errors before the failed build are kept with it";
        }

        const auto* arm = row_named(rows, "test ubuntu-24.04-arm llvm@23.1.3 ci");
        expect(arm && paths(*arm) == std::vector<std::string>{"modules/projects/meng/kernel/dynamics/cts"});
        if (arm) expect(arm->members[0].failures.at(0).why == "timeout after 300s");
    };

    "a failure is known when a red row names its package, the package its CTS checks, its suite or its program"_test = [] {
        const auto reds = ci::read_red_list(red_list);
        expect(reds.size() == 4u && reds[0].number == 55 && reds[3].state == "open") << reds.size();

        const auto rows = ci::read_log(read(here() / "data" / "run-43685505783.failed.log"));
        const auto* web = row_named(rows, "test web wasm32-emscripten");
        expect(web != nullptr);
        if (!web) return;
        const auto& nav = web->members.at(0);
        const auto names = ci::names_of(nav, nav.failures.at(0), "meng.nav.cts");
        expect(std::ranges::contains(names.package, std::string("meng.nav"))) << "a CTS member's package checks its owner";
        const auto nav_known = ci::known_in(names, reds);
        expect(nav_known.rows.empty() && nav_known.package_rows == std::vector<int>{96}) << "by its package alone: a hint";

        const auto* mac = row_named(rows, "test macos-15 llvm@23.1.3 ci");
        expect(mac != nullptr && mac->members.size() == 3);
        if (!mac || mac->members.size() != 3) return;
        const auto& sdk = mac->members[0];
        expect(ci::known_in(ci::names_of(sdk, sdk.failures.at(0), "omui.sdk"), reds).rows == std::vector<int>{55}) << "by its program";
        const auto& sdl3 = mac->members[1];
        expect(ci::known_in(ci::names_of(sdl3, sdl3.failures.at(0), "omui.host.sdl3"), reds).rows == std::vector<int>{55})
            << "by its suite's label, the end of the package's name";
        const auto& quay = mac->members[2];
        const auto quay_known = ci::known_in(ci::names_of(quay, quay.failures.at(0), "quay.play"), reds);
        expect(quay_known.rows.empty() && quay_known.package_rows.empty()) << "new: no row names it";
        const auto* windows = row_named(rows, "test windows-2022 llvm@23.1.3 ci");
        expect(windows != nullptr && windows->members.size() == 3);
        if (windows && windows->members.size() == 3) {
            const auto& bench = windows->members[1];
            expect(ci::known_in(ci::names_of(bench, bench.failures.at(0), "omui.bench"), reds).rows == std::vector<int>{82}) << "by its path";
        }
        expect(ci::known_in(ci::Names{{"mrender"}, {}}, reds).rows.empty()) << "a name stands whole: `mrender` is not `mrender.mesh`";
    };

    "one job's log as the API gives it (no job or step columns) is one row"_test = [] {
        const auto rows = ci::read_log("2026-11-15T10:52:33.1923284Z nav_test ... FAIL (exit 1, 0.69s, 13 cases)\n"
                                       "2026-11-15T10:52:33.1929533Z   FAIL nav/a crowd walks  (1 of 15 checks failed)\n"
                                       "2026-11-15T10:52:34.0000000Z    Workspace member 'modules/projects/meng/game/nav/cts' (68/83) FAILED — 1 passed, 1 failed, run 0.69s\n");
        expect(rows.size() == 1u && rows[0].name == "log" && rows[0].members.size() == 1u) << rows.size();
        if (rows.size() == 1u && rows[0].members.size() == 1u) expect(rows[0].members[0].failures.at(0).cases == std::vector<std::string>{"nav/a crowd walks"});
    };

    "a manifest's package"_test = [] {
        expect(ci::package_of("[package]\nnamespace = \"meng\"\nname = \"nav\"\n\n[dependencies]\nname = \"x\"\n") == "meng.nav");
        expect(ci::package_of("[package]\nname   = \"site\"   # the site\n") == "site");
        expect(ci::package_of("[workspace]\n").empty());
    };
}};
