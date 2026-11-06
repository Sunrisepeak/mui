// mlibs.testing.api:target — what the target the tests run on can do, as
// constants (chosen by the manifest per target; no macro). Here: a target
// that runs threads.
export module mlibs.testing.api:target;

export namespace mlibs::testing {

// Whether the program can start threads (std::thread, std::jthread).
inline constexpr bool threads = true;

}  // namespace mlibs::testing
