// openmui.showcase.widget_breakout — 《组件打砖块：闹鬼的设置》 (S4, D-50): the
// Settings panel is haunted, and every control works.
//
//     omui::run(omui::breakout::app(), argc, argv);
export module omui.showcase.widget_breakout;

export import omui;
export import omui.showcase.widget_breakout.sim;
export import :field_contract;
export import :rooms;
export import :fx;
export import :breakout_sound;
export import :haunt_music;
export import :sound;
export import :wide_ts;
export import :pierce_ts;
export import :slow_ts;
export import :multi_ts;
export import :magnet_ts;
export import :workshop;
export import :field;
export import :copilot;
export import :playtest;

export namespace omui::breakout {

// Registers the field: scripts, view documents and agents reach it by name.
void register_components();

// The application: a run's first room, filling the window.
[[nodiscard]] AppSpec app();

}  // namespace omui::breakout
