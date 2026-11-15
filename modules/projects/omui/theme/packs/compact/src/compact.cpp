module omui.theme.compact;

import std;
import omui.theme;

namespace omui::themes {

std::expected<void, theme::Diagnostics> add_compact() { return theme::library().add(compact_theme, "compact.theme.omui.json"); }

}  // namespace omui::themes
