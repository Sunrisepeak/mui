// Logical places and their public addresses. Legacy names remain aliases.
export module omui.site:routes;

import std;
export import :catalog;
import :motion_catalog;

export namespace omui::site {

struct Place {
    std::string_view id, en, zh;
    bool navigation = true;
    std::string_view route, section;
    bool document = false;  // read as a document (plain, no figure over it); the rest are places to use
};

[[nodiscard]] inline std::span<const Place> places() {
    static constexpr std::array<Place, 15> all{{
        {"home", "Home", "首页", false, "", "home"},
        // The places (the site plan §4): components, motion, the agent, the docs.
        {"components", "Components", "组件", true, "components", "components"},
        // Motion: the signature works, the recipe library and the compositions (once "Creative demos").
        {"explore", "Motion", "动效", true, "motion", "explore"},
        {"agent", "Agent native", "Agent 原生", true, "agent", "agent"},
        {"docs", "Docs", "文档", true, "docs", "docs", true},
        {"features", "Core ideas", "核心概念", false, "docs/features", "docs", true},
        {"practices", "Practical guide", "实践指南", false, "docs/practices", "docs", true},
        {"hello", "Get started", "开始使用", false, "docs/start", "docs", true},
        // Reference, under the docs: the motion specimens (once "Basic animation"), styling, one interface in many languages.
        {"motion", "Motion specimens", "动效规格样本", false, "docs/motion", "docs"},
        {"styles", "Styling", "风格化", false, "docs/styles", "docs"},
        {"interface", "One interface × languages", "统一接口 × 多语言", false, "docs/interface", "docs"},
        {"timeline", "Timeline editor", "时间线编辑器", false, "explore/timeline", "explore"},
        {"reactions", "Reaction lab", "反应实验室", false, "explore/reactions", "explore"},
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
    // An example: the motion page's compositions (motion/…; once demos/…), the motion
    // specimens (docs/motion/…; once motion/… — an older address still finds its specimen).
    for (const auto& [prefix, sets] : {std::pair{std::string_view("docs/motion/"), std::string_view("basic")}, std::pair{std::string_view("motion/"), std::string_view("both")},
                                       std::pair{std::string_view("demos/"), std::string_view("creative")}}) {
        if (!route.starts_with(prefix)) continue;
        const auto id = route.substr(prefix.size());
        if (sets != "basic")
            for (const auto& item : creative_motions())
                if (item.id == id) return SiteRoute{"explore", std::move(lang), {}, std::string(id)};
        if (sets != "creative")
            for (const auto& item : basic_motions())
                if (item.id == id) return SiteRoute{"motion", std::move(lang), {}, std::string(id)};
        return std::nullopt;
    }
    // Older addresses of places that moved: the motion page was demos; styling and the interface were at the top.
    if (route == "demos") return SiteRoute{"explore", std::move(lang)};
    if (route == "styles" || route == "interface") return SiteRoute{std::string(route), std::move(lang)};
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
        if (const auto parsed = parse_site_route(candidate); parsed && parsed->place == place) return std::string(language == "zh" ? "zh/" : "") + candidate;
    }
    if (language != "zh") return std::string(p->route);
    return p->route.empty() ? std::string("zh") : "zh/" + std::string(p->route);
}

[[nodiscard]] inline bool is_document(std::string_view place) {
    const Place* p = find_place(place);
    return p && p->document;
}

}  // namespace omui::site
