// openmui.bench:scenes — the framework's benchmarks.
//
// The reference scene is M1 acceptance ⑤ (implementation plan §19): a
// dashboard of 1000+ nodes — cards with a title, an action button, a value and
// a progress bar — shown at 1920×1080 and drawn by the CPU renderer. Its
// benchmarks time what a frame costs when nothing changed, when one label
// changes, when every label does, while the pointer sweeps across buttons and
// while the window resizes; each one reports the runtime's phase times and the
// node counts as counters. The others measure pieces alone: rasterising the
// whole 1080p display list, mounting the scene, the semantic snapshot an
// agent reads, shaping a paragraph, and what tracing and logging cost when off.
export module openmui.bench:scenes;

import std;

export namespace omui::bench {

// Registers the benchmarks below with the harness (the first call does).
void add_framework_benchmarks();

// The reference scene's shape, for tests: nodes after the first frame, and the
// list of benchmark names add_framework_benchmarks() registers.
struct SceneFacts {
    std::size_t nodes = 0;
    std::size_t cards = 0;
    float       width = 0, height = 0;
};
[[nodiscard]] SceneFacts reference_scene_facts();

}  // namespace omui::bench
