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
    register_type(component_type<Progress>());
    register_type(component_type<Radio>());
    register_type(component_type<Select>());
    register_type(component_type<SelectPopup>());
    register_type(component_type<SelectOption>());
    register_type(component_type<Tabs>());
    register_type(component_type<Tab>());
    register_type(component_type<Menu>());
    register_type(component_type<MenuItem>());
    register_type(component_type<Dialog>());
    register_type(component_type<Tooltip>());
}

}  // namespace omui
