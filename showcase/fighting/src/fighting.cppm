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
export import :voice_sound;
export import :fighters_sound;
export import :voice_files;
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

// How the application starts: at the title (no fighters named), or straight
// in a fight — who plays whom where, the computer's player, turns (agents
// playing by correspondence: Stage's turn_frames).
struct Start {
    std::string  player1, player2, arena;
    std::int32_t cpu = -1, cpu_level = 2, turn_frames = 0;
};

// The application: the title and the select screen, then the fight filling the window.
[[nodiscard]] AppSpec app(Start start = {});

}  // namespace omui::fighting
