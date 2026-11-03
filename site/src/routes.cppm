// Logical places and their public addresses. Legacy names remain aliases.
export module openmui.site:routes;

import std;
export import :catalog;

export namespace omui::site {

struct Place {
    std::string_view id, en, zh;
    bool navigation = true;
    std::string_view route, section;
};

[[nodiscard]] inline std::span<const Place> places() {
    static constexpr std::array<Place, 9> all{{
        {"home", "Home", "首页", false, "", "home"},
        {"components", "Components", "组件", true, "components", "components"},
        {"explore", "Explore", "探索", true, "explore", "explore"},
        {"docs", "Docs", "文档", true, "docs", "docs"},
        {"features", "Core ideas", "核心概念", false, "docs/features", "docs"},
        {"practices", "Practical guide", "实践指南", false, "docs/practices", "docs"},
        {"hello", "Get started", "开始使用", false, "docs/start", "docs"},
        {"timeline", "Timeline editor", "时间线编辑器", false, "explore/timeline", "explore"},
        {"brand", "The mark", "标志", false, "explore/brand", "explore"},
    }};
    return all;
}

[[nodiscard]] inline const Place* find_place(std::string_view id) {
    for (const Place& p : places())
        if (p.id == id) return &p;
    return nullptr;
}

struct SiteRoute {
    std::string place, language, component;
};

[[nodiscard]] inline std::optional<SiteRoute> parse_site_route(std::string_view route, std::string_view language = "en") {
    if (route.starts_with('#')) route.remove_prefix(1);
    while (route.starts_with('/')) route.remove_prefix(1);
    while (route.ends_with('/') && !route.empty()) route.remove_suffix(1);
    std::string lang(language == "zh" ? "zh" : "en");
    if (route == "zh" || route.starts_with("zh/")) {
        lang = "zh";
        route = route.size() > 2 ? route.substr(3) : std::string_view();
    } else if (route == "en" || route.starts_with("en/")) {
        lang = "en";
        route = route.size() > 2 ? route.substr(3) : std::string_view();
    }
    if (route.starts_with("components/")) {
        if (const auto* c = component_at(route.substr(11))) return SiteRoute{"components", std::move(lang), std::string(c->id)};
        return std::nullopt;
    }
    for (const Place& p : places())
        if (route == p.id || route == p.route) return SiteRoute{std::string(p.id), std::move(lang)};
    return std::nullopt;
}

[[nodiscard]] inline std::string canonical_route(std::string_view place, std::string_view language = "en", std::string_view component = {}) {
    if (place == "components" && find_component(component)) return std::string(language == "zh" ? "zh/" : "") + "components/" + component_slug(component);
    const Place* p = find_place(place);
    if (!p) return {};
    if (language != "zh") return std::string(p->route);
    return p->route.empty() ? std::string("zh") : "zh/" + std::string(p->route);
}

[[nodiscard]] inline bool is_document(std::string_view place) {
    const Place* p = find_place(place);
    return p && p->section == "docs";
}

}  // namespace omui::site
