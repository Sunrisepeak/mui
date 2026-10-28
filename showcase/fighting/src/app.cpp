// The application: the stage filling the window, and the stage registered by
// name for view documents, scripts and agents.
module openmui.showcase.fighting;

import std;
import openmui;

namespace omui::fighting {

void register_components() { register_type(component_type<Stage>()); }

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.fighting";
    a.title = "格斗游戏";
    a.size = {1280, 720};
    a.setup = [](Runtime&) { register_components(); };
    a.view = [] { return stage({.id = "stage", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}); };
    return a;
}

}  // namespace omui::fighting
