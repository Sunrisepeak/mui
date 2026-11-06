// Exercise the actual product entrances and history after the information
// architecture changed; a route table alone cannot verify the menu's input.
import std;
import openmui;
import openmui.site;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;

namespace {
struct Visitor {
    Headless h;
    explicit Visitor(std::string_view route = "home", float width = 1280, std::string_view lang = "en")
        : h(HeadlessOptions{.size = {width, 844}, .reduce_motion = true, .parallel_raster = false}) {
        auto app = site::app(route, lang);
        h.runtime().set_theme(app.theme); app.setup(h.runtime());
        h.mount("main", app.view()); h.settle(seconds(1));
    }
    void click(std::string_view id) { check(h.click(id), id); h.settle(seconds(0.5)); }
    void core(std::string_view id) {
        click("main/bar/nav/core/open");
        click(std::string("main/bar/nav/core/menu/to_") + std::string(id));
    }
};
const std::filesystem::path& media() { static const auto dir = scratch_dir("site-navigation"); return dir; }
}

int main(int argc, char** argv) {
    Suite suite{"site.navigation_redesign"};
    suite.test("every basic and creative detail has a canonical, validated bilingual address", [] {
        for (const auto language : {"en", "zh"}) {
            for (bool creative : {false, true}) for (const auto& item : creative ? site::creative_motions() : site::basic_motions()) {
                const std::string place = creative ? "explore" : "motion";
                const auto route = site::canonical_route(place, language, {}, item.id);
                const auto parsed = site::parse_site_route(route);
                check(parsed && parsed->place == place && parsed->language == language && parsed->example == item.id);
            }
            for (const auto mode : {"use", "develop", "design"}) {
                const auto parsed = site::parse_site_route(site::canonical_route("agent", language, {}, mode));
                check(parsed && parsed->example == mode && parsed->place == "agent");
            }
        }
        check(!site::parse_site_route("motion/does-not-exist"));
        check(!site::parse_site_route("demos/does-not-exist"));
        check(!site::parse_site_route("agent/does-not-exist"));
        check(site::parse_site_route("explore")->place == "explore", "old exploration links remain valid");
    });
    suite.test("one core menu opens real feature pages; Agent and creative demos are top-level", [] {
        Visitor v;
        check(!v.h.tree().node("main/bar/nav/to_components"));
        for (const auto id : {"components", "motion", "styles", "interface"}) {
            v.core(id);
            check(v.h.tree().node(std::string("main/body/view/") + id + "/page").has_value(), id);
        }
        v.click("main/bar/nav/to_explore");
        check(v.h.runtime().route() == "demos");
        v.click("main/bar/nav/to_agent");
        check(v.h.tree().node("main/body/view/agent/page/agent_feature").has_value());
        v.click("main/bar/nav/to_docs");
        check(v.h.tree().node("main/body/view/docs/page/directory").has_value());
        v.click("main/bar/nav/core/open");
        const auto open = v.h.tree().node("main/bar/nav/core/open");
        check(open && open->states.has(State::collapsible) && open->states.has(State::expanded), "the disclosure announces its open state");
        v.h.key(Key::escape); v.h.settle(seconds(0.3));
        const auto menu_tree = v.h.tree().snapshot({.root = "main/bar/nav/core"});
        check(std::ranges::none_of(menu_tree.nodes, [](const auto& node) { return node.id.starts_with("main/bar/nav/core/menu/"); }),
              "Escape removes closed-menu entries from the accessible snapshot");
        check(!v.h.runtime().get(v.h.runtime().find("main/bar/nav/core/menu"), "open").as_bool(), "Escape closes the actual popover");
        const auto closed = v.h.tree().node("main/bar/nav/core/open");
        check(closed && closed->states.has(State::collapsible) && !closed->states.has(State::expanded), "the disclosure announces its collapsed state");
    });
    suite.test("history follows example and agent subroutes without losing their meaning", [] {
        Visitor v("zh/motion/list-grid");
        check(v.h.runtime().route() == "zh/motion/list-grid");
        for (const auto route : {"demos/packing-board", "agent/develop", "agent/design", "components/toggle", "motion", "zh/demos"}) {
            v.h.runtime().set_route(route); v.h.settle(seconds(0.6));
            const auto parsed = site::parse_site_route(route);
            check(parsed.has_value());
            check(v.h.tree().node(std::string("main/body/view/") + parsed->place + "/page").has_value(), route);
            check(v.h.runtime().route() == route, "following history must not rewrite its address");
        }
    });
    suite.test("both languages and mobile menus retain one visible top-level heading", [] {
        for (const auto language : {"en", "zh"}) for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) {
            Visitor v("home", width, language);
            for (const auto route : {"home", "components", "motion", "styles", "interface", "agent", "demos"}) {
                v.h.runtime().set_route(site::canonical_route(route == std::string_view("demos") ? "explore" : route, language));
                v.h.settle(seconds(0.35));
                std::size_t headings = 0;
                const auto tree = v.h.tree().snapshot({.root = "main/body/view"});
                for (const auto& node : tree.nodes)
                    if (node.role == Role::heading && v.h.runtime().get(v.h.runtime().find(node.id), "level").as_int() == 1) ++headings;
                check(headings == 1, std::format("{} {} {}: {} top-level headings", route, language, width, headings));
                if (width == 390 || width == 1280)
                    check(v.h.save_png(media() / std::format("{}-{}-{}.png", route, language, static_cast<int>(width))).has_value());
            }
            if (width < 900) {
                v.click("main/bar/menu_toggle"); v.core("components");
                check(v.h.tree().node("main/body/view/components/page/gallery").has_value());
            }
        }
    });
    suite.test("resizing inside one responsive band recomputes the mounted specimen width", [] {
        for (const auto route : {"home", "components", "motion", "demos", "styles", "interface", "agent"}) {
            Visitor v(route, 390);
            v.h.runtime().resize({360, 844}, 1); v.h.settle(seconds(0.5));
            const auto place = site::parse_site_route(route)->place;
            for (const auto& node : v.h.tree().snapshot({.root = "main/body/view/" + place + "/page"}).nodes) {
                if (!node.bounds || node.states.has(State::hidden)) continue;
                if (node.role == Role::button || node.role == Role::slider || node.role == Role::switch_ || node.role == Role::text_box)
                    check(node.bounds->x >= 19.5f && node.bounds->right() <= 340.5f, std::format("{} resized: {}", route, node.id));
            }
        }
    });
    suite.test("global dark mode updates plain preview scopes while keeping explicit materials", [] {
        Visitor v("components");
        v.click("main/bar/settings_toggle");
        Invocation call; call.args = Value{}.set("value", "dark");
        check(v.h.tree().act("main/body/settings/theme_field/theme", "set_value", call).exit_code() == std::optional<int>(0));
        v.h.settle(seconds(0.5));
        check(v.h.runtime().theme().dark);
        const auto id = v.h.runtime().find("main/body/view/components/page/gallery/detail/button/stage/specimen/variants/primary");
        check(v.h.runtime().valid(id), "the actual plain specimen is mounted");
        if (v.h.runtime().valid(id)) check(v.h.runtime().theme_of(id).dark, "plain preview follows the chosen base theme");
    });
    return suite.run(argc, argv);
}
