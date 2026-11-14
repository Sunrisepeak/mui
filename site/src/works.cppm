// openmui.site:works — the motion page's signature works (the site plan §7.1,
// E1): each one capability you can take in your hands — its stage live, its
// name, one line, its controls (play, pause, a time to drag): the mark
// writing itself; Mui's pixels gathering; a soft landing rewound; a line
// changing language in place; the same items as a list and as cards; a
// spring you can throw; an agent changing a motion's timing; glass that
// flows (two drops of liquid glass joining and parting).
export module omui.site:works;

import std;
import omui;
import hanim.engine;
import :program_stage;
import :live_wall;

export namespace omui::site {

struct Works {
    // Each work's time and whether it plays (the stages write them; the controls set them). A work
    // made of nothing at its start (the mark unwritten, the pixels away) rests at its end until played.
    Signal<double> mark_at = signal(1e6), mark_length = signal(0.0), gather_at = signal(1e6), gather_length = signal(0.0), card_at = signal(0.0);
    Signal<bool>   mark_playing = signal(false), gather_playing = signal(false), other = signal(false), cards = signal(false);
    // The cards an agent retimes: their time, timing and plan (the stage writes the plan, as hanim made it).
    Signal<double> rise_at = signal(1e6), rise_length = signal(0.0), rise_delay = signal(0.0), rise_bounce = signal(0.0);
    Signal<bool>   rise_playing = signal(false), together = signal(false);
    Signal<std::string> rise_plan = signal(std::string());
    // The recipe library's filter: all, entrance, exit, attention, loop.
    Signal<std::string> recipe_kind = signal(std::string("all"));
    // The recipe in its editor and its tuning (its own to start: see tune_recipe), its time and whether it plays.
    Signal<std::string> tune_name = signal(std::string("pulse"));
    Signal<double>      tune_amount = signal(1.08), tune_amount_y = signal(1.08), tune_duration = signal(0.0), tune_period = signal(0.0);
    Signal<double>      tune_at = signal(0.0), tune_length = signal(0.0);
    Signal<bool>        tune_playing = signal(false);
    // The library alive: a few cards on screen playing at random, the one under the pointer playing.
    LiveWall            recipe_live;
};

// A recipe into the library's editor, tuned as it comes (its own parameter, duration and period).
void tune_recipe(Works&, std::string_view name);

struct WorksSite {
    Runtime**            runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::string          root = "main/body/view/explore/page/works";
};

[[nodiscard]] Element signature_works(Works&, const WorksSite&, float width);
// The works one by one (ids mark, gather, soft, letters, spring, agent, glass, layout), each
// `card` wide; `path_of(id)` is where that card's node is (its controls act on its stage there).
[[nodiscard]] std::vector<Element> work_cards(Works&, const WorksSite&, float card, const std::function<std::string(std::string_view)>& path_of);
// The ids of the works, in order.
inline constexpr std::array<std::string_view, 8> work_ids{"mark", "gather", "soft", "letters", "spring", "agent", "glass", "layout"};
// The recipe library (the site plan §7.2): every recipe of hanim's library by
// name (anim::recipes()), each a live thumbnail — played on a card when asked
// — with its call in C++ and its step in an animation document to copy.
[[nodiscard]] Element recipe_library(Works&, const WorksSite&, float width);
void recipe_library_tick(Works&, const WorksSite&);
// A recipe's card: its stage, its name, play, its call and step to copy, Tune (into the editor,
// then `tuning`). `card_path` is where the card's node is; the pointer on it calls `hover`.
[[nodiscard]] Element recipe_card(Works&, const WorksSite&, const hanim::anim::RecipeInfo&, float width, std::string card_path,
                                  std::function<void(bool)> hover, std::function<void()> tuning);
// The editor: the recipe being tuned on a larger stage, its sliders, its call and step to copy.
[[nodiscard]] Element recipe_editor(Works&, const WorksSite&, float width);
// A recipe's call (C++) and its step (an animation document's JSON): as tuned, or with its defaults.
[[nodiscard]] std::string recipe_cpp(std::string_view name, const WorkTuning& tuning = {});
[[nodiscard]] std::string recipe_json(std::string_view name, const WorkTuning& tuning = {});

}  // namespace omui::site
