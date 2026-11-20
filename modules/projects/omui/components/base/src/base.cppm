// omui.ui.base — the Core components (spec §11) as behaviour: each class
// derives from the CRTP base omui-gen made from its contract, and uses the
// runtime's public API only (component-binding §1). The look is the theme's.
// Beside them, the package's own (its contract/ directory, made into code
// when it builds): badge, avatar, skeleton, card, accordion, toast, popover,
// breadcrumb, pagination, number input; who else acts here (actor log);
// segmented, chip, range slider, alert, stepper; date, time and colour
// pickers; split view, drawer; tree view, data grid; combobox, tag input;
// code editor.
//
//     import omui.ui.base;
//     omui::register_base_components();   // view documents and immediate mode find them by name
//     rt.mount("main", omui::column({.gap = 8}, {
//         omui::button({.id = "save", .text = "Save", .variant = omui::ButtonVariant::primary}),
//         omui::slider({.id = "volume", .value = volume, .label = "Volume"}),
//     }));
export module omui.ui.base;

export import omui.core;
export import omui.view;
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
export import :agent_presence_contract;
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
export import :tree_view_contract;
export import :data_grid_contract;
export import :combobox_contract;
export import :tag_input_contract;
export import :code_editor_contract;
export import :kbd_contract;
export import :separator_contract;
export import :link_contract;
export import :description_list_contract;
export import :spinner_contract;
export import :rating_contract;
export import :otp_input_contract;
export import :form_field_contract;
export import :search_box_contract;
export import :toggle_group_contract;
export import :context_menu_contract;
export import :hover_card_contract;
export import :command_palette_contract;
export import :sidebar_contract;
export import :toolbar_contract;
export import :chart_contract;
export import :carousel_contract;
export import :file_drop_contract;
export import :web_view_contract;
export import :timeline_contract;
export import :path_contract;
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
export import :collections;
export import :entry;
export import :code;
export import :reading;
export import :inputs;
export import :navigating;
export import :showing;
export import :external;
export import :timeline;
export import :path;

export namespace omui {

// The base components, one list: what `register_base_components()` registers, and what an application's derived
// registry picks from by name (B7b).
template <class... Component>
struct ComponentList {};
using BaseComponents = ComponentList<Button, Toggle, Checkbox, Slider, TextBox, Progress,
                                     Radio, Select, SelectPopup, SelectOption, Tabs, Tab,
                                     Menu, MenuItem, Dialog, Tooltip, List, ListItem,
                                     Badge, Avatar, Skeleton, Card, Accordion, Toast,
                                     Popover, Breadcrumb, Pagination, NumberInput, ActorLog, AgentPresence,
                                     Segmented, Chip, RangeSlider, Alert, Stepper, Timeline,
                                     Path, DatePicker, TimePicker, ColorPicker, SplitView, Drawer,
                                     TreeView, DataGrid, Combobox, TagInput, CodeEditor, Kbd,
                                     Separator, Link, DescriptionList, Spinner, Rating, OtpInput,
                                     FormField, SearchBox, ToggleGroup, ContextMenu, HoverCard, CommandPalette,
                                     Sidebar, Toolbar, Chart, Carousel, WebView, FileDrop>;

// A component's name as its contract has it (`button`, `text_box`), a template argument.
template <std::size_t Size>
struct ComponentName {
    char text[Size]{};
    consteval ComponentName(const char (&name)[Size]) { std::copy_n(name, Size, text); }
    [[nodiscard]] constexpr std::string_view view() const { return {text, Size - 1}; }
};

// Registers the components' types with the runtime's registry, so view
// documents and immediate mode (`ui.button`) find them by contract name.
template <class... Component>
void register_components(ComponentList<Component...>) {
    (register_type(component_type<Component>()), ...);
}
void register_base_components();

}  // namespace omui

// The registry's helpers, the module's own: reachable where register_base_named is instantiated, not named outside.
namespace omui {

template <class Component, ComponentName... Names>
void register_if_named() {
    // compare(), a member: found where the registry is instantiated (a unit of the application's that need not import std).
    if constexpr (((Component::name.compare(Names.view()) == 0) || ...)) register_type(component_type<Component>());
}
template <ComponentName... Names, class... Component>
void register_named(ComponentList<Component...>) {
    (register_if_named<Component, Names...>(), ...);
}

}  // namespace omui

export namespace omui {

// Registers, of the base components, only those named (the contract's names; a name that is no base component —
// a layout's, a function's — names none): what an application's build writes from its scripts' imports (omui.build's
// closure(), B7b). The others are not referred to, so a program that registers this way does not carry them.
template <ComponentName... Names>
void register_base_named() {
    register_named<Names...>(BaseComponents{});
}

}  // namespace omui
