module openmui.theme.default_theme;

import std;
import openmui.theme;

namespace omui::themes {

std::expected<void, theme::Diagnostics> add_default() { return theme::library().add(default_theme, "default.theme.omui.toml"); }

}  // namespace omui::themes
