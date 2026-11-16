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
//           the count), `omui-gen wasm-size` over it — sections, code by owner, largest functions — and, given a
//           budget (<name>.budget.omui.json), its bytes held to it.
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
//           verdicts are kept in target/sweep/<program>.sweep.omui.jsonl, its output beside them.
// toy       the Toy row (D-84 §6.5, §8.9): the games built for wasm32-emscripten and packed for Bilibili Toy
//           (`mcpp pack --pkg-format dist-toy`, omui.build.toy's dist_toy()), each checked by Toy's own
//           toy_doctor.py — the platform's checker, pinned to github.com/bilibili/toy at ae156ec (MIT), downloaded
//           into target/toy and held to its sha256. Every package's sizes are said; Toy's hard limit, index.html at
//           most 1 MiB, is the packer's error and the doctor's. Nothing is published (the maintainer's
//           `toy create`). The packages are copied to target/toy/<target>/ with <target>.toy.omui.json beside.
// tools-e2e the products' commands as an Agent uses them (convergence plan §10.3): hanim's, xrender's and the
//           timeline's, each operation through its real command line, its exit status and JSON answer held.
// agent-e2e M1 acceptance ②: omui agent operating example 07 as its own process (needs-answer for an irreversible
//           action), the development operations, the MCP server over real pipes, and the Reaction lab's verbs as
//           MCP tools from its generated listing.
// media     the showcases' README media (each a signature shot its tests draw headless into the scratch directory
//           readme-<showcase>: a still, or an animated WebP or PNG under 1 MiB; deterministic, so committed in
//           showcase/<showcase>/media/), and the site's stills and two animations under site/media/: run the
//           tests and copy what they drew, or (`check`) hold the committed files to what the tests drew.
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
};

[[nodiscard]] int features(const std::filesystem::path& root);
[[nodiscard]] int size(const std::filesystem::path& root, const Size& asked);
[[nodiscard]] int templates(const std::filesystem::path& root);
[[nodiscard]] int reflect(const std::filesystem::path& root, std::string_view profile);
[[nodiscard]] int sweep(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
[[nodiscard]] int toy(const std::filesystem::path& root, std::vector<std::string> packages);
[[nodiscard]] int tools_e2e(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
[[nodiscard]] int agent_e2e(const std::filesystem::path& root, const std::vector<std::string>& profile_args);
// shows: the showcases' directory names and "site"; none, every showcase and the site.
[[nodiscard]] int media(const std::filesystem::path& root, std::vector<std::string> shows, bool check_only);
[[nodiscard]] int atspi(const std::filesystem::path& root, const std::vector<std::string>& profile_args);

// A template's file rendered: each {{name}} replaced by its value; or the first name it has no value for.
[[nodiscard]] std::expected<std::string, std::string> render(std::string_view text, const std::map<std::string, std::string>& values);

// The measured bytes held to a budget file's {"budgets": [{"metric", "max"}]}: each one over it, as a message.
[[nodiscard]] std::vector<std::string> over_budget(const std::map<std::string, std::uint64_t>& measured, std::string_view budget_text);

}  // namespace omui::devtools::check
