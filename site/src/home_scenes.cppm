// The home's scenes under its opening (the site plan §5, 10–60 s): a screen
// each, a heading and at most one line, then the thing itself, live —
// ① the whole page as pixels, as its structure, as an agent reads it (the
// site's own lens); ② a line switching language, the letters both share
// kept where they are; ③ a soft card let go, its time dragged back and played on
// the same, step for step; ④ the page saved as SVG, every shape named by the
// node that drew it (the host's file offer: the Web's download).
export module omui.site:home_scenes;

import std;
import omui;

export namespace omui::site {

struct HomeScenes {
    Signal<bool>   other = signal(false);  // ② the line in the other language
    Signal<double> at = signal(0.0);       // ③ the card's time
    // ④ the last picture saved: its size, its shapes named by their nodes (or why it was not).
    Signal<std::int64_t> saved_bytes = signal(std::int64_t{0}), saved_shapes = signal(std::int64_t{0});
    Signal<std::string>  saved_error = signal(std::string());
};

struct HomeScenesSite {
    Runtime**            runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    Signal<std::string>* lens = nullptr;   // the site's lens: pixels, tree, agent, test
    Signal<bool>*        tools = nullptr;  // the foot's tools (the lenses among them): shown with a lens
    std::string          root = "main/body/view/home/page/scenes";
};

[[nodiscard]] Element home_scenes(HomeScenes&, const HomeScenesSite&, float width);

}  // namespace omui::site
