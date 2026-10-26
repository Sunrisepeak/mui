// openmui.ui.base — the Core components (spec §11) as behaviour: each class
// derives from the CRTP base omui-gen made from its contract, and uses the
// runtime's public API only (component-binding §1). The look is the theme's.
//
//     import openmui.ui.base;
//     omui::register_base_components();   // view documents and immediate mode find them by name
//     rt.mount("main", omui::column({.gap = 8}, {
//         omui::button({.id = "save", .text = "Save", .variant = omui::ButtonVariant::primary}),
//         omui::slider({.id = "volume", .value = volume, .label = "Volume"}),
//     }));
export module openmui.ui.base;

export import openmui.core;
export import openmui.view;
export import :parts;
export import :button_contract;
export import :toggle_contract;
export import :checkbox_contract;
export import :slider_contract;
export import :text_box_contract;
export import :progress_contract;
export import :radio_contract;
export import :select_contract;
export import :tabs_contract;
export import :tab_contract;
export import :menu_contract;
export import :menu_item_contract;
export import :dialog_contract;
export import :tooltip_contract;
export import :button;
export import :toggle;
export import :slider;
export import :text_box;
export import :progress;
export import :tabs;
export import :menu;
export import :dialog;
export import :select;

export namespace omui {

// Registers the components' types with the runtime's registry, so view
// documents and immediate mode (`ui.button`) find them by contract name.
void register_base_components();

}  // namespace omui
