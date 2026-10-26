module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

void register_base_components() {
    register_type(component_type<Button>());
    register_type(component_type<Toggle>());
    register_type(component_type<Checkbox>());
    register_type(component_type<Slider>());
    register_type(component_type<TextBox>());
}

}  // namespace omui
