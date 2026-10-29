// openmui.showcase.fighting — the fighting game (D-85 §8.7).
//
//     omui::run(omui::fighting::app(), argc, argv);
export module openmui.showcase.fighting;

export import openmui;
export import gongfu.present2d;
export import gongfu.training;
export import :stage_contract;
export import :gongfu_fighters;
export import :roster;
export import :hud;
export import :sound;
export import :fight_sound;
export import :study_stage;
export import :street_stage;
export import :study_music;
export import :street_music;
export import :mo_parts_art;
export import :mo_fx_art;
export import :mo_cutin_eyes_art;
export import :mo_cutin_strike_art;
export import :mo_finisher_art;
export import :mo_faces_art;
export import :ni_parts_art;
export import :ni_cutin_eyes_art;
export import :ni_cutin_strike_art;
export import :ni_finisher_art;
export import :stage;

export namespace omui::fighting {

// Registers the stage: scripts, view documents and agents reach it by name.
void register_components();

// The application: the stage, filling the window.
[[nodiscard]] AppSpec app();

}  // namespace omui::fighting
