// Logical places and their public addresses. Legacy names remain aliases.
export module openmui.site:routes;

import std;
export import :catalog;
import :motion_catalog;

export namespace omui::site {

struct Place {
    std::string_view id, en, zh;
    bool navigation = true;
    std::string_view route, section;
};

[[nodiscard]] inline std::span<const Place> places() {
    static constexpr std::array<Place, 14> all{{
        {"home", "Home", "首页", false, "", "home"},
        {"components", "UI components", "UI 组件", true, "components", "core"},
        {"motion", "Basic animation", "基础动画", true, "motion", "core"},
        {"styles", "Styling", "风格化", true, "styles", "core"},
        {"interface", "One interface × languages", "统一接口 × 多语言", true, "interface", "core"},
        {"explore", "Creative demos", "创意 demos", true, "demos", "explore"},
        {"agent", "Agent native", "Agent 原生", true, "agent", "agent"},
        {"docs", "Docs", "文档", true, "docs", "docs"},
        {"features", "Core ideas", "核心概念", false, "docs/features", "docs"},
        {"practices", "Practical guide", "实践指南", false, "docs/practices", "docs"},
        {"hello", "Get started", "开始使用", false, "docs/start", "docs"},
        {"timeline", "Timeline editor", "时间线编辑器", false, "explore/timeline", "explore"},
        {"brand", "The mark", "标志", false, "explore/brand", "explore"},
        {"release", "Release workspace", "发布工作台", false, "scenes/release", "explore"},
    }};
    return all;
}

[[nodiscard]] inline const Place* find_place(std::string_view id) {
    for (const Place& p : places())
        if (p.id == id) return &p;
    return nullptr;
}

struct SiteRoute {
    std::string place, language, component, example;
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
    for (const auto& [prefix, place, creative] : {
             std::tuple{"motion/", "motion", false}, std::tuple{"demos/", "explore", true}}) {
        if (!route.starts_with(prefix)) continue;
        const auto id = route.substr(std::string_view(prefix).size());
        for (const auto& item : creative ? creative_motions() : basic_motions())
            if (item.id == id) return SiteRoute{place, std::move(lang), {}, std::string(id)};
        return std::nullopt;
    }
    if (route.starts_with("agent/")) {
        const auto mode = route.substr(6);
        if (mode == "use" || mode == "develop" || mode == "design")
            return SiteRoute{"agent", std::move(lang), {}, std::string(mode)};
        return std::nullopt;
    }
    for (const Place& p : places())
        if (route == p.id || route == p.route) return SiteRoute{std::string(p.id), std::move(lang)};
    return std::nullopt;
}

[[nodiscard]] inline std::string canonical_route(std::string_view place, std::string_view language = "en", std::string_view component = {}, std::string_view example = {}) {
    if (place == "components" && find_component(component)) return std::string(language == "zh" ? "zh/" : "") + "components/" + component_slug(component);
    const Place* p = find_place(place);
    if (!p) return {};
    if (!example.empty()) {
        const std::string candidate = std::string(p->route) + "/" + std::string(example);
        if (parse_site_route(candidate)) return std::string(language == "zh" ? "zh/" : "") + candidate;
    }
    if (language != "zh") return std::string(p->route);
    return p->route.empty() ? std::string("zh") : "zh/" + std::string(p->route);
}

[[nodiscard]] inline bool is_document(std::string_view place) {
    const Place* p = find_place(place);
    return p && p->section == "docs";
}

}  // namespace omui::site
