module shelf;

import std;
import omui.core;
import omui.view;

namespace shelf {

void register_components() { omui::register_type(omui::component_type<Rating>()); }

}  // namespace shelf
