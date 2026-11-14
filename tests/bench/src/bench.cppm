// omui.bench — the framework's benchmarks (:scenes), the fighting game's
// (:game) and the others', and how a body is measured (:harness: mtest's laps
// under a scene's loop). tests/budgets_test.cpp runs each as a case whose
// budgets are its bounds; tests/bench_test.cpp keeps the loop and the scenes honest.
export module omui.bench;

export import :harness;
export import :scenes;
export import :game;
export import :audio;
export import :public_motion;
export import :simulation;
export import :render;
