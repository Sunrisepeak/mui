// openmui.showcase.fighting — the fighting game (D-85 §8.7).
//
//     omui::run(omui::fighting::app(), argc, argv);
export module openmui.showcase.fighting;

export import openmui;
export import gongfu.present2d;
export import gongfu.training;
export import :stage_contract;
export import :kata_fighter;
export import :kata_look;
export import :roster;
export import :hud;
export import :stage;

export namespace omui::fighting {

// Registers the stage: scripts, view documents and agents reach it by name.
void register_components();

// The application: the stage, filling the window.
[[nodiscard]] AppSpec app();

}  // namespace omui::fighting
