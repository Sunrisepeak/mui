// Product exhibits, learning and references have distinct entrances.
export module omui.site:wayfinding;

import std;
import omui;

export namespace omui::site {

struct SiteLinks {
    Signal<std::string>* language = nullptr;
    std::function<void(std::string)> go;
    std::function<void(std::string)> open_url;
    std::function<std::string_view(std::string_view, std::string_view)> document;
};

struct DocsState {
    Signal<std::string> query = signal(std::string());
};

inline constexpr std::string_view quickstart_commands = "xlings install mcpp\nmcpp new hello --template openmui\ncd hello\nmcpp run";

[[nodiscard]] Element docs_directory(const SiteLinks& links, std::string current, bool vertical);
[[nodiscard]] Element docs_landing(DocsState& docs, const SiteLinks& links, float width);
[[nodiscard]] Element explore_landing(const SiteLinks& links, float width);
[[nodiscard]] Element home_destinations(const SiteLinks& links, float width);
[[nodiscard]] std::string docs_html(bool zh);
[[nodiscard]] std::string explore_html(bool zh);
[[nodiscard]] std::string destinations_html(bool zh);

}  // namespace omui::site
