// mlibs.cts — the conformance framework every contract's suite runs on
// (convergence plan §5.5): :core (suites, cases, passed/failed/unsupported,
// the report), :compare (bytes, tolerances, pictures, pinned hashes,
// sampled processes) and :prepare (the preparation contract's cases: accepted
// the same way twice, refusals by kind that leave the engine as it was). Suites live with their contract's owner, in `src/cts/`
// under the feature `cts`, and run in that owner's tests and its
// implementations' tests.
export module mlibs.cts;

export import :core;
export import :compare;
export import :prepare;
