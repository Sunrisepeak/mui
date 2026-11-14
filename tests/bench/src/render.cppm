// omui.bench:render — what the 3D renderer's parts cost before a pixel is drawn (the foundation plan's M6.3):
// xrender.scene's extraction — what a view sees of ten thousand instances, by every instance and by the tree —
// and its mirror following a frame's changes.
export module omui.bench:render;

import std;

export namespace omui::bench {

// Registers them with the harness (add_framework_benchmarks calls it).
void add_render_benchmarks();

}  // namespace omui::bench
