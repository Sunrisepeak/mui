// openmui.bench:game — what the fighting game costs (D-85 §8.6 performance):
// one frame of gongfu's simulation, the eight frames played again after a
// rollback (a state restored and re-simulated), and building one frame's
// picture with gongfu.present2d. The fighters are the game's own files, the
// inputs a busy match's (directions held, buttons, motions), the same every run.
export module openmui.bench:game;

import std;

export namespace omui::bench {

// Registers the game's benchmarks with the harness (add_framework_benchmarks calls it).
void add_game_benchmarks();

}  // namespace omui::bench
