// Exercise the actual product entrances and history after the information
// architecture changed; a route table alone cannot verify the menu's input.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
struct Visitor {
    Headless h;
    explicit Visitor(std::string_view route = "home", float width = 1280, std::string_view lang = "en")
        : h(HeadlessOptions{.size = {width, 844}, .reduce_motion = true, .parallel_raster = false}) {
        auto app = site::app(route, lang);
        h.runtime().set_theme(app.theme); app.setup(h.runtime());
        h.mount("main", app.view()); h.settle(seconds(1));
    }
    void click(std::string_view id) { expect(h.click(id)) << id; h.settle(seconds(0.5)); }
    // A reference page, from the docs' list.
    void reference(std::string_view id) {
        click("main/bar/nav/to_docs");
        const auto open = std::format("main/body/view/docs/page/read/results/{}/open", id);
        expect(h.tree().act(open, "scroll_into_view").exit_code() == std::optional<int>(0)) << open;
        h.settle(seconds(0.3));
        click(open);
    }
};
const std::filesystem::path& media() { static const auto dir = scratch_dir("site-navigation"); return dir; }
}

UTest cases{"site.navigation_redesign", [] {
    "every basic and creative detail has a canonical, validated bilingual address"_test = [] {
        for (const auto language : {"en", "zh"}) {
            for (bool creative : {false, true}) for (const auto& item : creative ? site::creative_motions() : site::basic_motions()) {
                const std::string place = creative ? "explore" : "motion";
                const auto route = site::canonical_route(place, language, {}, item.id);
                const auto parsed = site::parse_site_route(route);
                expect(parsed && parsed->place == place && parsed->language == language && parsed->example == item.id);
            }
            for (const auto mode : {"use", "develop", "design"}) {
                const auto parsed = site::parse_site_route(site::canonical_route("agent", language, {}, mode));
                expect(parsed && parsed->example == mode && parsed->place == "agent");
            }
        }
        expect(!site::parse_site_route("motion/does-not-exist"));
        expect(!site::parse_site_route("demos/does-not-exist"));
        expect(!site::parse_site_route("docs/motion/does-not-exist"));
        expect(!site::parse_site_route("agent/does-not-exist"));
        expect(site::parse_site_route("explore")->place == "explore") << "old exploration links remain valid";
        // The places that moved: their older addresses still find them.
        expect(site::parse_site_route("motion")->place == "explore" && site::parse_site_route("demos")->place == "explore");
        expect(site::parse_site_route("zh/demos/packing-board")->example == "packing-board" && site::parse_site_route("demos/packing-board")->place == "explore");
        expect(site::parse_site_route("motion/list-grid")->place == "motion") << "a specimen's older address finds it under the docs";
        expect(site::canonical_route("motion", "en", {}, "list-grid") == "docs/motion/list-grid");
        expect(site::canonical_route("explore", "zh", {}, "packing-board") == "zh/motion/packing-board");
        for (const auto place : {"styles", "interface"}) expect(site::parse_site_route(place)->place == place && site::canonical_route(place) == std::string("docs/") + place);
    };
    "four places at the top: components, motion, the agent, the docs; the reference under the docs"_test = [] {
        Visitor v;
        const auto nav = v.h.tree().snapshot({.root = "main/bar/nav"});
        std::vector<std::string> buttons;
        for (const auto& node : nav.nodes)
            if (node.role == Role::button) buttons.push_back(node.name);
        expect(buttons == std::vector<std::string>{"Components", "Motion", "Agent native", "Docs"}) << "one row of places, no menu";
        v.click("main/bar/nav/to_components");
        expect(v.h.tree().node("main/body/view/components/page").has_value());
        v.click("main/bar/nav/to_explore");
        expect(v.h.runtime().route() == "motion");
        expect(v.h.tree().node("main/body/view/explore/page/wall/canvas/work-mark/mark/stage").has_value()) << "the motion page opens on its works, live on the studio";
        v.click("main/bar/nav/to_agent");
        expect(v.h.tree().node("main/body/view/agent/page/agent_feature").has_value());
        v.click("main/bar/nav/to_docs");
        expect(v.h.tree().node("main/body/view/docs/page/directory").has_value());
        for (const auto id : {"motion", "styles", "interface"}) {
            v.reference(id);
            expect(v.h.tree().node(std::string("main/body/view/") + id + "/page").has_value()) << id;
            expect(v.h.runtime().route() == std::string("docs/") + id);
            const auto docs = v.h.tree().node("main/bar/nav/to_docs");
            expect(docs && docs->states.has(State::selected)) << "a reference page is the docs' place";
        }
    };
    "history follows example and agent subroutes without losing their meaning"_test = [] {
        Visitor v("zh/docs/motion/list-grid");
        expect(v.h.runtime().route() == "zh/docs/motion/list-grid");
        for (const auto route : {"motion/packing-board", "agent/develop", "agent/design", "components/toggle", "docs/motion", "zh/motion"}) {
            v.h.runtime().set_route(route); v.h.settle(seconds(0.6));
            const auto parsed = site::parse_site_route(route);
            expect(parsed.has_value());
            expect(v.h.tree().node(std::string("main/body/view/") + parsed->place + "/page").has_value()) << route;
            expect(v.h.runtime().route() == route) << "following history must not rewrite its address";
        }
    };
    "both languages and mobile menus retain one visible top-level heading"_test = [] {
        for (const auto language : {"en", "zh"}) for (const float width : {360.0f, 390.0f, 768.0f, 1280.0f, 1440.0f}) {
            Visitor v("home", width, language);
            for (const auto route : {"home", "components", "motion", "styles", "interface", "agent", "explore"}) {
                v.h.runtime().set_route(site::canonical_route(route, language));
                v.h.settle(seconds(0.35));
                std::size_t headings = 0;
                const auto tree = v.h.tree().snapshot({.root = "main/body/view"});
                for (const auto& node : tree.nodes)
                    if (node.role == Role::heading && v.h.runtime().get(v.h.runtime().find(node.id), "level").as_int() == 1) ++headings;
                expect(headings == 1) << std::format("{} {} {}: {} top-level headings", route, language, width, headings);
                if (width == 390 || width == 1280)
                    expect(v.h.save_png(media() / std::format("{}-{}-{}.png", route, language, static_cast<int>(width))).has_value());
            }
            if (width < 900) {
                v.click("main/bar/menu_toggle"); v.click("main/bar/nav/to_components");
                expect(v.h.tree().node("main/body/view/components/page/wall").has_value());
            }
        }
    };
    "resizing inside one responsive band recomputes the mounted specimen width"_test = [] {
        for (const auto route : {"home", "components", "motion", "docs/motion", "docs/styles", "docs/interface", "agent"}) {
            Visitor v(route, 390);
            v.h.runtime().resize({360, 844}, 1); v.h.settle(seconds(0.5));
            const auto place = site::parse_site_route(route)->place;
            const auto nodes = v.h.tree().snapshot({.root = "main/body/view/" + place + "/page"}).nodes;
            // What a sideways scroll view holds may lie beyond the page (a row swiped sideways): the scroll view fits instead.
            std::vector<std::string> scrollers;
            for (const auto& node : nodes)
                    if (const Value* room = node.value.get("max_x"); node.role == Role::scroll_view && room && room->as_double(0) > 0) scrollers.push_back(node.id + "/");
            const auto scrolled = [&](const std::string& id) { return std::ranges::any_of(scrollers, [&](const std::string& in) { return id.starts_with(in); }); };
            for (const auto& node : nodes) {
                if (!node.bounds || node.states.has(State::hidden) || scrolled(node.id)) continue;
                if (node.role == Role::button || node.role == Role::slider || node.role == Role::switch_ || node.role == Role::text_box || node.role == Role::scroll_view)
                    expect(node.bounds->x >= 19.5f && node.bounds->right() <= 340.5f) << std::format("{} resized: {}", route, node.id);
            }
        }
    };
    "global dark mode updates plain preview scopes while keeping explicit materials"_test = [] {
        Visitor v("components/button");  // the library: a component's page
        v.click("main/bar/settings_toggle");
        Invocation call; call.args = Value{}.set("value", "dark");
        expect(v.h.tree().act("main/body/settings/theme_field/theme", "set_value", call).exit_code() == std::optional<int>(0));
        v.h.settle(seconds(0.5));
        expect(v.h.runtime().theme().dark);
        const auto id = v.h.runtime().find("main/body/view/components/page/gallery/detail/button/stage/specimen/variants/primary");
        expect(v.h.runtime().valid(id)) << "the actual plain specimen is mounted";
        if (v.h.runtime().valid(id)) expect(v.h.runtime().theme_of(id).dark) << "plain preview follows the chosen base theme";
    };
}};
