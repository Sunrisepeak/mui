// A small opening: one real object and three ways to meet the same UI.
export module openmui.site:home_landing;

import std;
import openmui;
export import :brand;

export namespace omui::site {

struct HomeLanding {
    Signal<std::string> mode = signal(std::string("pixels"));
    Signal<bool> unfolded = signal(true), paused = signal(false), manual = signal(false);
    Signal<std::string> status = signal(std::string("playing")), outcome = signal(std::string()), part_names = signal(std::string());
    NodeId preview;
    std::string visual_path, scene_path, pause_path;
    std::uint64_t timer = 0, generation = 0;
    bool introduced = false, reading_active = false;
    std::shared_ptr<unsigned char> lifetime = std::make_shared<unsigned char>(0);
};

struct HomeLandingSite {
    Runtime** runtime = nullptr;
    Signal<std::string>* lang = nullptr;
    std::function<void(std::string)> go;
    std::string root = "main/body/view/home/page/landing";
};

[[nodiscard]] Element home_landing(HomeLanding&, const HomeLandingSite&, float width);
void tick_home_landing(HomeLanding&, const HomeLandingSite&);
void cancel_home_landing(HomeLanding&, const HomeLandingSite&);
[[nodiscard]] std::string home_landing_html(bool zh);

}  // namespace omui::site
