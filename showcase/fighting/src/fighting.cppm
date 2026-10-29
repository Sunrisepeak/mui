// openmui.showcase.fighting — the fighting game (D-85 §8.7).
//
//     omui::run(omui::fighting::app(), argc, argv);
export module openmui.showcase.fighting;

export import openmui;
export import gongfu.present2d;
export import gongfu.training;
export import :stage_contract;
export import :select_contract;
export import :gongfu_fighters;
export import :roster;
export import :hud;
export import :sound;
export import :fight_sound;
export import :menu_sound;
export import :stage_files;
export import :stage_art_files;
export import :music_files;
export import :pad_controls;
export import :art_files;
export import :hud_files;
export import :stage;
export import :select;

export namespace omui::fighting {

// Registers the stage: scripts, view documents and agents reach it by name.
void register_components();

// The application: the title and the select screen, then the fight filling the window.
[[nodiscard]] AppSpec app();

}  // namespace omui::fighting
