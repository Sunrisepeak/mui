// openmui.site:works — the motion page's signature works (the site plan §7.1,
// E1): each one capability you can take in your hands — its stage live, its
// name, one line, its controls (play, pause, a time to drag). The first:
// the mark writing itself; Mui's pixels gathering; a soft landing rewound;
// a line changing language in place.
export module omui.site:works;

import std;
import omui;

export namespace omui::site {

struct Works {
    // Each work's time and whether it plays (the stages write them; the controls set them).
    Signal<double> mark_at = signal(0.0), mark_length = signal(0.0), gather_at = signal(0.0), gather_length = signal(0.0), card_at = signal(0.0);
    Signal<bool>   mark_playing = signal(false), gather_playing = signal(false), other = signal(false);
};

struct WorksSite {
    Runtime**            runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::string          root = "main/body/view/explore/page/works";
};

[[nodiscard]] Element signature_works(Works&, const WorksSite&, float width);

}  // namespace omui::site
