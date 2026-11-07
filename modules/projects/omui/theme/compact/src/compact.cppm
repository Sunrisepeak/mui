// openmui.theme.compact — the compact theme file, carried in the program.
export module omui.theme.compact;

export import :compact_theme;

import std;
import omui.theme;

export namespace omui::themes {

// Adds what compact.theme.omui.toml declares to the program's theme library, once
// (again: the same, a no-op).
std::expected<void, theme::Diagnostics> add_compact();

}  // namespace omui::themes
