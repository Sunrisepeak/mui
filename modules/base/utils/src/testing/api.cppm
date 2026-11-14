// mlibs.testing.api — what openmui's test programs share beyond their test
// framework (mcpplibs.mtest, D-147): a named scratch directory whose files
// stay for a person to look at (README media are read from it), the target's
// constants (threads), and the FNV-1a hash of pinned determinism values.
// The checks, cases and suites are mtest's; this is not a test framework.
export module mlibs.testing.api;

import std;
export import mlibs.hash;
export import mlibs.target;

export namespace mlibs::testing {

// FNV-1a over bytes, for the pinned hashes of determinism tests: one value per
// sampled table keeps the tests short and makes a cross-platform difference a
// single, comparable line in the CI log (mlibs.hash's: bytes, u64, f64,
// f32, str, value, hex).
using Hash = ::mlibs::hash::Fnv1a;

// Whether the program can start threads: the target's (mlibs.target).
using ::mlibs::target::threads;

// A directory a test may write into, removed when the program starts again.
[[nodiscard]] std::filesystem::path scratch_dir(std::string_view test_name);

}  // namespace mlibs::testing
