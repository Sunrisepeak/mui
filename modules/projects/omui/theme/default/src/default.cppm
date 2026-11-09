// openmui.theme.default_theme — the default theme file, carried in the program.
export module omui.theme.default_theme;

export import :default_theme;
// A staged overlay (staged/refined.theme.omui.ts): carried as text for what
// compares it with the defaults, never added to the library by add_default().
export import :refined_theme;

import std;
import omui.theme;

export namespace omui::themes {

// Adds what default.theme.omui.ts declares to the program's theme library, once
// (again: the same, a no-op).
std::expected<void, theme::Diagnostics> add_default();

}  // namespace omui::themes
