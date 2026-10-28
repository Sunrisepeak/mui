// The application: level 1's field filling the window, and the field
// registered by name for view documents, scripts and agents.
module openmui.showcase.widget_breakout;

import std;
import openmui;

namespace omui::breakout {

void register_components() { register_type(component_type<Field>()); }

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.breakout";
    a.title = "组件打砖块";
    a.size = {1280, 720};
    a.theme = Theme::dark_theme();
    a.setup = [](Runtime&) { register_components(); };
    a.view = [] { return field({.id = "field", .level = 1, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, level_rows(level(1))); };
    return a;
}

}  // namespace omui::breakout
