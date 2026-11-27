// omui.devtools.check — CI's acceptance steps (moved from tools/ci/*.sh, D-206). Each builds what it needs
// through mcpp (its products from mcpp's own record), runs it and holds the result; it says `ok …` for each
// thing it showed, `::error::…` for what failed, and returns the step's exit status. A workflow's step is the one
// line that calls it (the allowlist's first kind).
//
// features  M0 acceptance ⑤ (plan §19.2): a feature selects one of two mutually exclusive source files. Each
//           choice builds and passes its test — tests/features with its default `sink-stdout` (a workspace member),
//           and an application asking for `sink-memory` (tools/ci/features-consumer, built standalone: a workspace
//           build plans every member together and would give the fixture both) — and asking for both is refused
//           before anything compiles: both files provide the partition `omui.features:sink`.
// size      where a page's WebAssembly bytes go (W8, Q-38; the foundation plan's M6.3 budget): a package built for
//           wasm32-emscripten as a release is (-O2, the wasm-size profile: its functions' names kept, left out of
//           the count), `omui-gen size` over it — sections, code by owner, largest functions — and, given a
//           budget (<name>.budget.xdef.json), its bytes held to it.
// templates the templates (component-binding §10.1), rendered as `mcpp new --template` renders a published one
//           ({{var}} in *.in files, the suffix dropped; until omui is in mcpp-index), their omui dependencies
//           pointed at this checkout, each built and tested: an application out of the tree as a user makes one;
//           a package that runs a build rule (component, native, theme) as a temporary member of this workspace
//           (mcpp offers a path build-dependency to a build program only from a workspace that builds it).
// reflect   omui.reflect (C++26 reflection, D-39), gated on a toolchain that can build it in omui's graph (V-08):
//           libstdc++'s `std` built with -freflection does not merge the C library's unnamed-struct typedefs
//           (__mbstate_t first) with a unit that includes a C header in its global module fragment, and a unit
//           built without it cannot read that `std`. A canary first — the smallest such graph, in a directory of
//           its own: while it fails with that conflict the suite cannot be built and this says so (a warning);
//           once a toolchain builds it, the suite runs and must pass; any other canary failure fails the step.
// sweep     the interaction sweep (strategy §4, a hard gate, D-91): every program run headless as its own process
//           and `omui agent sweep` over it — every node an agent can operate, every action performed (what can be
//           undone, undone; what a person must answer, only asked). A dead control fails it. Each program's
//           verdicts are kept in target/sweep/<program>.sweep.xdef.jsonl, its output beside them.
// toy       the Toy row (D-84 §6.5, §8.9): the games built for wasm32-emscripten and packed for Bilibili Toy
//           (`mcpp pack --pkg-format dist-toy`, omui.build.toy's dist_toy()), each checked by Toy's own
//           toy_doctor.py — the platform's checker, pinned to github.com/bilibili/toy at ae156ec (MIT), downloaded
//           into target/toy and held to its sha256. Every package's sizes are said; Toy's hard limit, index.html at
//           most 1 MiB, is the packer's error and the doctor's. Nothing is published (the maintainer's
//           `toy create`). The packages are copied to target/toy/<target>/ with <target>.toy.xdef.json beside.
// tools-e2e the products' commands as an Agent uses them (convergence plan §10.3): hanim's, mrender's and the
//           timeline's, each operation through its real command line, its exit status and JSON answer held.
// agent-e2e M1 acceptance ②: omui agent operating example 07 as its own process (needs-answer for an irreversible
//           action), the development operations, the MCP server over real pipes, and the Reaction lab's verbs as
//           MCP tools from its generated listing.
// media     the showcases' README media (each a signature shot its tests draw headless into the scratch directory
//           readme-<showcase>: a still, or an animated WebP or PNG under 1 MiB; deterministic, so committed in
//           showcase/<showcase>/media/), and the site's stills and two animations under site/media/: run the
//           tests and copy what they drew, or (`check`) hold the committed files to what the tests drew.
// distribution  the smallest application's distribution (B4, D-214): example 01 packed for this machine as a
//           directory; Inter alone of the fonts (at most 0.9 MB), no window system's library.
// android   omui on Android (W10): example 01 packed into an APK (mcpp.plugins' dist-apk; the platform's
//           NativeActivity, no Java) and checked inside — the application's library, the C++ runtime, the default
//           fonts with their licences; then, where the machine can run an emulator (KVM), installed on Android 7.0
//           (API 24, x86_64), started and driven through the platform's own input: it says it runs, a tap on its
//           button changes what it shows, Back with nothing open ends it. The emulator, its system image and adb are
//           xlings packages; an AVD is made here (no avdmanager), its data in /dev/shm where that has room (the
//           emulator asks for 7 GB free). Screenshots go to `out`.
// web       the Web row (D-84): every member whose manifest says it runs on the Web, host.web and the site built for
//           wasm32-emscripten and tested under node with the same pinned hashes as every row (the determinism gate for
//           the browser too); the page halves' JavaScript under node's test runner with a fake DOM, WebGPU and WebGL2;
//           the yard's bytes held to their budget (size); then browser.
// browser   what only a real page can check (D-84 §6.3), where a headless Chromium is found (omui.devtools.browser):
//           mrender.rhi.webgpu's and .webgl's suites, their consumers' WebGPU suites run by their owners (hanim.rhi,
//           the cellular kernel), the mesh pass's contract on both, Quay's frame, host.web drawing on each (its fonts
//           preloaded), and every WGSL and ESSL entry point the Web build wrote, compiled by the browser. Without a
//           Chromium the checks are said to be skipped, a failure only with OMUI_REQUIRE_BROWSER=1.
// dist-toy-loading  omui.gen's dist_toy tests, then the generated loader they wrote run against the browser's own
//           fetch, streams and WebAssembly over real gzip HTTP: failures, retries, shared font transfers
//           (target/dist-toy-loading.report.xdef.json).
// live-page the design shown in place and the customization of a page as it ships (G8): a page built for the Web
//           (example 13's) in a real browser through window.omui — the replay, a look and a view document committed,
//           kept across a reload, exported and imported to the same picture (target/live-page.json).
// site      omui's site, its one page (G5), built for the Web, in a real browser: its delivery within its budget
//           (site/delivery.budget.xdef.json), then the visitor's path — the Live Design key, Copy, the circle breathing
//           while it waits, the visitor's agent connected by `omui connect <code>` and named with its omui and platform,
//           the bridge stopped, reduced motion (target/site-perf.json, target/site.json).
// atspi     examples 03 and 07 built and read and operated through AT-SPI (`omui-devtools atspi`) on a private
//           session bus (dbus-run-session), as a screen reader does. Linux x86_64 (AccessKit's library);
//           at-spi2-core.
export module omui.devtools.check;

import std;

export namespace omui::devtools::check {

struct Size {
    std::string package;             // e.g. omui.site
    std::filesystem::path json;      // the report (default: target/<package>.wasm-size.json)
    std::filesystem::path markdown;  // and as a table, when asked
    std::filesystem::path budget;    // the budgets, when it has them
    bool native = false;             // this machine's program (its symbols kept), not the Web's: built at each profile the
                                     // budget's rows name (budget_profiles; `size`, dist's code, where they name none)
};

[[nodiscard]] int features(const std::filesystem::path& root);
[[nodiscard]] int size(const std::filesystem::path& root, const Size& asked);
// `only`: one template by its name (app-ts, oweb:docs); empty: every one.
[[nodiscard]] int templates(const std::filesystem::path& root, std::string_view only = {});
[[nodiscard]] int reflect(const std::filesystem::path& root, std::string_view profile);
[[nodiscard]] int sweep(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
[[nodiscard]] int toy(const std::filesystem::path& root, std::vector<std::string> packages);
[[nodiscard]] int tools_e2e(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
[[nodiscard]] int agent_e2e(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
// shows: the showcases' directory names and "site"; none, every showcase and the site.
[[nodiscard]] int media(const std::filesystem::path& root, std::vector<std::string> shows, bool check_only);
[[nodiscard]] int android(const std::filesystem::path& root, const std::filesystem::path& out);
[[nodiscard]] int distribution(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
[[nodiscard]] int web(const std::filesystem::path& root, std::string_view profile);
[[nodiscard]] int browser_checks(const std::filesystem::path& root);

// The page script that compiles every WGSL and ESSL entry of these shader partitions (mlibs.shader's `<entry>_wgsl` and
// `<entry>_essl` byte arrays) with the browser's own compilers and links each file's vertex and fragment entries.
[[nodiscard]] std::expected<std::string, std::string> shader_check_script(std::span<const std::filesystem::path> partitions);

// CI's matrix on this one Linux machine (lint, GCC dev and release, LLVM, the budgets, features, templates, agent e2e, and
// the Windows target under wine where wine is): `members` empty, the whole workspace.
[[nodiscard]] int matrix(const std::filesystem::path& root, const std::vector<std::string>& members);
[[nodiscard]] int atspi(const std::filesystem::path& root, const std::vector<std::string>& profile_args, std::string_view screen = {},
                        bool compose = false);

// The design shown in place and the customization of a page as it ships (G8): the page `built` (example 13's when
// empty) in a real browser through window.omui — the replay, a look previewed, committed and undone, a view document
// in a surface, a reload, the layer exported and imported to the same picture (target/live-page.json).
[[nodiscard]] int live_page(const std::filesystem::path& root, const std::filesystem::path& built);
// omui's site (G5) as it ships: `built` (site/'s `omui build --web` when empty) — these parts (none: both): "delivery"
// within its budget, "path" the visitor's with an agent connected by omui connect (omui: built when not given).
[[nodiscard]] int site(const std::filesystem::path& root, const std::filesystem::path& built, const std::filesystem::path& omui,
                       std::vector<std::string> parts);
// The generated loader against the browser's own streams, failures and retries (fixture: omui.gen's dist_toy test
// run first and its own used, when not given).
[[nodiscard]] int dist_toy_loading(const std::filesystem::path& root, const std::filesystem::path& fixture);
// A built page with a look (`omui build --web --look`) in headless Chromium, its program held until its document
// is read: each word a reader sees, where the page's document sets it and where the layer does once the program
// has presented — at each width, at the top and scrolled. Held to the bound (UI5): at most 2 px, half of them at
// most 1 px, none to another line. `path`: the page among a site's ("/start/"; "/" the directory's own). The report:
// target/takeover.report.xdef.json.
[[nodiscard]] int takeover(const std::filesystem::path& root, const std::filesystem::path& page, std::string_view path, std::span<const int> widths);

// A built site's pages scrolled by the wheel in a headed Chromium on a monitor of the desktop (red 164): each route
// in turn, round after round — at rest, the wheel down to the page's end and back up — measured by the browser's own
// trace and the page's long tasks and animation frames, the machine's load beside each phase. The report:
// target/scroll.report.xdef.json (with `keep_trace`, each phase's trace in target/scroll/).
struct Scroll {
    std::filesystem::path    page;             // a site built with its engine (oweb site.build --engine)
    std::vector<std::string> paths;            // its routes, visited in turn ("/long/", "/start/")
    int                      rounds = 2;
    std::string              screen;           // the monitor (RANDR's name: HDMI-0); empty: the primary
    int                      width = 1280, height = 900;  // the window's
    double                   speed = 3000;     // how fast the wheel turns, px a second
    std::string              query;            // after each route's address (?renderer=cpu)
    std::vector<std::string> flags;            // more switches for Chromium
    bool                     headless = false;  // no window: a headless Chromium (not a performance number)
    bool                     keep_trace = false;
    // The program's own spans recorded too (mlibs.trace through window.omui): where its frames go — a run for that,
    // not for the frame numbers (recording costs the program time of its own).
    bool                     spans = false;
};
[[nodiscard]] int scroll(const std::filesystem::path& root, const Scroll& asked);

// A template's file rendered: each {{name}} replaced by its value; or the first name it has no value for.
[[nodiscard]] std::expected<std::string, std::string> render(std::string_view text, const std::map<std::string, std::string>& values);

// The measured bytes held to a budget file's {"budgets": [{"metric", "max"}]}: each one over it, as a message. Given
// a profile, only the rows measured at it are held — a row's own `profile`, or `fallback` where it names none — and
// the messages say it.
[[nodiscard]] std::vector<std::string> over_budget(const std::map<std::string, std::uint64_t>& measured, std::string_view budget_text,
                                                   std::string_view profile = {}, std::string_view fallback = {});

// The profiles a budget file's rows are measured at, each once and in the rows' order: a row's `profile`, or
// `fallback` where it names none (check size --native's is `size`, dist's code; the terminal's tiers say `small`,
// compiled size-optimised). A file that is not JSON with its budgets: `fallback` alone.
[[nodiscard]] std::vector<std::string> budget_profiles(std::string_view budget_text, std::string_view fallback);

}  // namespace omui::devtools::check
