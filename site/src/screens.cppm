// openmui.site:screens — the home page's screens after its opening, one
// strength each (positioning §2.1): your agent works here as you do; every
// moment can be drawn again, the same as CI; one motion for every change
// (the page's scroll is its time); fast on every device.
export module openmui.site:screens;

import std;
import openmui;

export namespace omui::site {

// Their own state: the motion screen's drawing (false the first, true the
// second) and whether its morph is held to the scroll.
struct Screens {
    Signal<bool> shaped = signal(false);
    bool         held = false;
    double       at = 0;      // how far it is to be: 0 the list, 1 the cards
    double       last_y = 0;  // the home page's offset the bounds last drawn are at
};

// What they need of the site.
struct ScreensSite {
    Runtime**                runtime = nullptr;
    Signal<std::string>*     lang = nullptr;
    Signal<std::string>*     hash = nullptr;       // the tree's hash, as the foot says it
    Signal<std::string>*     effects = nullptr;    // the effects tier the person chose ("auto", "t1" … "t4")
    std::function<void()>    copy_prompt;          // the agent panel open, the prompt given
    std::function<void()>    open_agent;           // the agent panel open (its simulated agent there)
    std::function<void()>    open_check;           // the reference run against CI
    std::function<void()>    replay;               // the page's time played backwards
    std::function<void(std::string)> set_effects;  // as the settings set them
};

[[nodiscard]] Element agent_screen(const ScreensSite& site, float room);
[[nodiscard]] Element time_screen(const ScreensSite& site, float room);
[[nodiscard]] Element motion_screen(Screens& screens, const ScreensSite& site, float room);
[[nodiscard]] Element fast_screen(const ScreensSite& site, float room);

// The home page scrolled to `y` (its new offset; the glide there just
// begun): the motion screen's morph follows, the scroll its time (`place`
// the home's path: main/body/view/home).
void scrolled(Screens& screens, const ScreensSite& site, std::string_view place, double y);

}  // namespace omui::site
