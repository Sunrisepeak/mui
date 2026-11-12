// mlibs.cts — what a contract's conformance suite is written with, on mtest
// (mcpplibs.mtest.cts; convergence plan §5.5, T0.9): :core (a suite's cases
// declared as mtest's conformance cases against a subject — passed, failed, or
// unsupported from what the subject can do; what a case records; the cases
// counted in a program that is not a test), :compare (bytes, tolerances,
// pictures, pinned hashes, sampled processes) and :prepare (the preparation
// contract's cases: accepted the same way twice, refusals by kind that leave
// the engine as it was). The runner, the selection and the reports are
// mtest's. Suites live with their contract's owner and run in that owner's
// tests and its implementations' tests.
export module mlibs.cts;

export import :core;
export import :compare;
export import :prepare;
