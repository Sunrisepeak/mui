// omui.devtools.consumer — the out-of-tree consumer probes (moved from tools/ci/*consumer*, hanim-api-probe and
// hanim-features, D-206). Each makes a fresh project outside this repository from a fixture under tools/ci/, builds
// and runs it through mcpp as a user's project would (its own lock, then --locked), and audits what mcpp actually
// resolved and compiled (resolution.json, compile_commands.json, build.ninja): the closure a product brings, the
// sources it compiles, nothing of the UI or the test harness where it must not be. A report (JSON) and the
// commands' logs are kept beside it; the repository's own lock must not change.
//
// hanim-core    the public hanim.core and mlibs.log, alone (geometry, timeline, log records).
// hanim-api     animation-first authoring through the public `import hanim` umbrella, dev and release.
// hanim-play    a windowless Engine and vector consumer: pixels at times and crops, retargeting, the process path,
//               an Agent's review (61 frames, receipts, refusals), a refused capture, an invalid region; then its
//               tests in a second consumer.
// omui-hanim    the optional UI/hanim adapter in a headless application (61 frames of each kind, the same pixels in
//               every profile, on the Web too with --web); missing fonts refused; the adapter off leaves no player.
// hanim-features each feature of hanim chosen alone: the closure and exports follow it; the control path on the
//               Web; the packed product linked by a consumer without its implementation sources.
export module omui.devtools.consumer;

import std;

export namespace omui::devtools::consumer {

struct Options {
    std::filesystem::path output;  // the report (each probe has a default under target/)
    bool keep_project = false;     // leave the fresh project in the temporary directory
    bool web = false;              // omui-hanim: also on wasm32-emscripten
    std::filesystem::path openas;  // hanim-play: an unpacked OpenAS binary artifact to validate in place of the published one
};

// The probe's exit status (0 when its report says ok); it prints the summary line the scripts printed.
[[nodiscard]] int probe(const std::filesystem::path& root, std::string_view name, const Options& options);

// The probes' names.
[[nodiscard]] std::span<const std::string_view> names();

// A PNG's width and height from its header, or nothing where it is not a PNG.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> png_size(std::string_view bytes);

// The outputs build.ninja names (each `build OUT…:` line's outputs).
[[nodiscard]] std::set<std::string> ninja_outputs(std::string_view text);

}  // namespace omui::devtools::consumer
