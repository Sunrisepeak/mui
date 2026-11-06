// openmui:shown — how `omui::run` shows an application where the program
// runs: a desktop window (src/host/desktop), or the page's canvas
// (src/host/web). The manifest chooses the unit by the target (D-84).
module openmui:shown;

import std;
import openmui.core;
import openmui.view;
import :app;

namespace omui::detail {

// Mounts the application on the host's runtime; what it answers lives as
// long as the application is shown (the framework's frame).
using Prepare = std::function<std::shared_ptr<void>(Runtime&)>;

// Shows the application until it ends; answers the exit code.
int shown(const AppSpec& app, const RunOptions& o, const Prepare& prepare);

// The person's languages as the host states them.
[[nodiscard]] std::vector<std::string> host_locales();

}  // namespace omui::detail
