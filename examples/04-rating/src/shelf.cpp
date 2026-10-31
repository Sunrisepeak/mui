module shelf;

import std;
import openmui.core;
import openmui.view;

namespace shelf {

void register_components() { omui::register_type(omui::component_type<Rating>()); }

}  // namespace shelf
