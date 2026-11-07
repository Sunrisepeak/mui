// openmui.theme.default_theme — the default theme file, carried in the program.
export module omui.theme.default_theme;

export import :default_theme;

import std;
import omui.theme;

export namespace omui::themes {

// Adds what default.theme.omui.toml declares to the program's theme library, once
// (again: the same, a no-op).
std::expected<void, theme::Diagnostics> add_default();

}  // namespace omui::themes
