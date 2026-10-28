// openmui.testing:target — what the target the tests run on can do, as
// constants (chosen by the manifest per target; no macro). Here: a target
// that runs threads.
export module openmui.testing:target;

export namespace omui::testing {

// Whether the program can start threads (std::thread, std::jthread).
inline constexpr bool threads = true;

}  // namespace omui::testing
