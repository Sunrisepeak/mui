// openmui.showcase.widget_breakout — 《组件打砖块：openmui 巡礼》 (S4, D-50).
//
//     omui::run(omui::breakout::app(), argc, argv);
export module openmui.showcase.widget_breakout;

export import openmui;
export import openmui.showcase.widget_breakout.sim;
export import :field_contract;
export import :levels;
export import :breakout_sound;
export import :sound;
export import :wide_ts;
export import :pierce_ts;
export import :slow_lua;
export import :workshop;
export import :field;
export import :copilot;

export namespace omui::breakout {

// Registers the field: scripts, view documents and agents reach it by name.
void register_components();

// The application: level 1's field, filling the window.
[[nodiscard]] AppSpec app();

}  // namespace omui::breakout
