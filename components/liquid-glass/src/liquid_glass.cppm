// openmui.ui.liquid_glass — liquid glass as components (D-64, features §3.3).
//
//     import openmui.ui.liquid_glass;
//     omui::liquid_glass({.id = "toolbar", .radius = 22, .blur = 4}, {buttons…});
//     omui::glass_group({.smoothness = 18}, {
//         omui::liquid_glass({.id = "play", .radius = 28}, {icon(…)}),
//         omui::liquid_glass({.id = "seek", .radius = 20}, {slider(…)}),
//     });
//
// The interfaces are contract/*.contract.omui.toml (bases, builders, stubs
// and schema are generated when the package builds); the look is the
// renderer's built-in `liquid_glass` effect, reading what lies under the
// glass (OMUI_EXT_backdrop_effect). Register the components by name with
// register_liquid_glass_components() for view documents and scripts.
export module openmui.ui.liquid_glass;

export import openmui.core;
export import openmui.view;
export import :liquid_glass_contract;
export import :glass_group_contract;
export import :glass;
