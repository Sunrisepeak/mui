module openmui.theme.compact;

import std;
import openmui.theme;

namespace omui::themes {

std::expected<void, theme::Diagnostics> add_compact() { return theme::library().add(compact_theme, "compact.theme.omui.toml"); }

}  // namespace omui::themes
