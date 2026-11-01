// openmui.ui.base — the Core components (spec §11) as behaviour: each class
// derives from the CRTP base omui-gen made from its contract, and uses the
// runtime's public API only (component-binding §1). The look is the theme's.
// Beside them, the package's own (its contract/ directory, made into code
// when it builds): badge, avatar, skeleton, card, accordion, toast, popover,
// breadcrumb, pagination, number input; who else acts here (actor log);
// segmented, chip, range slider, alert, stepper; date, time and colour
// pickers; split view, drawer.
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
export import :list_contract;
export import :badge_contract;
export import :avatar_contract;
export import :skeleton_contract;
export import :card_contract;
export import :accordion_contract;
export import :toast_contract;
export import :popover_contract;
export import :breadcrumb_contract;
export import :pagination_contract;
export import :number_input_contract;
export import :actor_log_contract;
export import :segmented_contract;
export import :chip_contract;
export import :range_slider_contract;
export import :alert_contract;
export import :stepper_contract;
export import :date_picker_contract;
export import :time_picker_contract;
export import :color_picker_contract;
export import :split_view_contract;
export import :drawer_contract;
export import :button;
export import :toggle;
export import :slider;
export import :text_box;
export import :progress;
export import :tabs;
export import :menu;
export import :dialog;
export import :select;
export import :list;
export import :status;
export import :surfaces;
export import :navigation;
export import :number_input;
export import :actors;
export import :choices;
export import :notices;
export import :pickers;
export import :containers;

export namespace omui {

// Registers the components' types with the runtime's registry, so view
// documents and immediate mode (`ui.button`) find them by contract name.
void register_base_components();

}  // namespace omui
