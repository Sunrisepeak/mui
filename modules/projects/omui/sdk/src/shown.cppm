// openmui:shown — how `omui::run` shows an application where the program
// runs: a desktop window (src/host/desktop), or the page's canvas
// (src/host/web). The manifest chooses the unit by the target (D-84).
module omui:shown;

import std;
import omui.core;
import omui.view;
import :app;

// Not `detail`: omui.core exports a namespace of that name, and a partition's own declarations in it
// are not found by every compiler (NDK clang 30) once the exported one is imported.
namespace omui::hosting {

// Mounts the application on the host's runtime; what it answers lives as
// long as the application is shown (the framework's frame).
using Prepare = std::function<std::shared_ptr<void>(Runtime&)>;

// Shows the application until it ends; answers the exit code.
int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare);

// The person's languages as the host states them.
[[nodiscard]] std::vector<std::string> host_locales();

}  // namespace omui::hosting
