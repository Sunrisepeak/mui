// openmui.abi — openmui.h, implemented (spec §8).
//
// C callers include the header and link this library; C++ callers (tests,
// the bindings' own C++) import the module and call the same functions in
// omui::abi. A desktop host lends its window loop to omuiRun through
// set_window_runner (the shared library the bindings load installs one).
export module openmui.abi;

export import :c;
export import :api;

import std;
import openmui.core;
import openmui.view;

export namespace omui::abi {

// Shows a runtime in a window until it closes; answers the exit code.
using WindowRunner = std::function<int(Runtime& runtime, std::string_view app_id, std::function<void()> after_frame)>;
void set_window_runner(WindowRunner runner);

// The Runtime of a handle, for C++ that mixes both.
[[nodiscard]] Runtime* runtime_of(OmuiRuntime rt) noexcept;

}  // namespace omui::abi
