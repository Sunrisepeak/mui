// xengine.field.cts — what the air's tests and benchmarks share: the standard program, and the air over a fire (a
// cell held at 900 K under air covered 4 m about (0, 3, 0)), stepped a number of times through the subject.
export module xengine.field.cts;

import std;
export import xengine.field;
export import xengine.field.agent;
export import xengine.matter.json;

export namespace xengine::field::cts {

// The standard definition, prepared once.
[[nodiscard]] const matter::Program& standard_program();

// Air covered 4 m about (0, 3, 0), a fire's cell at (0.5, 0.5, 0.5) held at 900 K, stepped `steps` times.
[[nodiscard]] std::unique_ptr<agent::Air> air_over_a_fire(int steps);

}  // namespace xengine::field::cts
