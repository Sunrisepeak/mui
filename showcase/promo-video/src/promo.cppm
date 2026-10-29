// openmui.showcase.promo_video — S5, openmui's film (D-51; promo-video
// design): sixty seconds at 60 frames a second, and a fifteen-second cut,
// made with openmui. The film is data — a timeline a cut
// (*.timeline.omui.toml), its lines (*.subtitles.omui.toml), its score and
// sounds — and scene code: every shot is a live openmui scene (the settings
// panel a pixel belongs to, the counter in four languages, the Markdown
// wall, the style workshop, an agent at work, the haunted breakout), drawn
// on the headless host from its own time. So any frame renders on its own,
// the same bytes every time, and the film checks itself (`omui-promo check`).
//
//     auto film = omui::promo::load_film();
//     auto cut = omui::promo::load_cut(film->files, "promo", film->subtitles);
//     omui::promo::Player player(*film, *cut);
//     auto frame = player.frame(1800);    // the wall, half a minute in
export module openmui.showcase.promo_video;

export import :film;
export import :look;
export import :shot;
export import :overlay;
export import :player;
export import :sound;
export import :checks;

// The files the program carries (openmui.build.embed).
export import :timeline_files;
export import :subtitles_files;
export import :style_files;
export import :music_files;
export import :sound_files;
export import :page_files;
export import :ts_files;
export import :lua_files;
export import :py_files;
export import :cpp_files;
