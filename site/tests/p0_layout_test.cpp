// Presentation checks use actual font measurements, rendered geometry and
// pointer/keyboard input; semantic existence alone cannot prove reachability.
import std;
import omui;
import omui.site;
import omui.theme;
import omui.theme.styles;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
constexpr std::string_view home_root = "main/body/view/home/page/landing";

bool contains(Rect outer, Rect inner) {
    return inner.width > 0 && inner.height > 0 && inner.x >= outer.x - 0.5f && inner.y >= outer.y - 0.5f &&
           inner.right() <= outer.right() + 0.5f && inner.bottom() <= outer.bottom() + 0.5f;
}

bool hit_reaches(Runtime& runtime, NodeId target, Point point) {
    for (auto hit = runtime.hit_test(point); runtime.valid(hit); hit = runtime.parent(hit)) if (hit == target) return true;
    return false;
}

// Shape the complete label in the actual scoped font. Test that it fits the
// drawn control with padding, rather than merely fitting an outer viewport.
void readable_label(Runtime& runtime, NodeId id, std::string_view text, float horizontal_padding = 16) {
    const auto bounds = runtime.screen_bounds(id);
    const auto& token = runtime.theme_of(id).text_style("label");
    mlibs::text::ParagraphStyle style;
    style.style.families = token.families; style.style.size = token.size; style.style.weight = token.weight;
    style.style.line_height = token.line_height; style.style.letter_spacing = token.letter_spacing;
    mlibs::text::Paragraph label(runtime.fonts(), std::string(text), style);
    label.layout(std::numeric_limits<float>::infinity());
    expect(label.lines().size() == 1 && label.size().width + horizontal_padding <= bounds.width + 0.5f &&
          label.size().height <= bounds.height + 0.5f) << std::format("the complete label fits {}", runtime.path(id));
}

struct Styles {
    site::FeatureExamples state;
    Signal<std::string> language = signal(std::string("en"));
    Headless h;
    Runtime* runtime;
    site::FeatureExamplesContext context;
    Styles(float width, std::string_view lang)
        : h(HeadlessOptions{.size = {width + 40, 844}, .tier = mlibs::pipeline::Tier::t4}), runtime(&h.runtime()) {
        language.set(std::string(lang)); context.runtime = &runtime; context.lang = &language;
        context.root = "main/viewport/styles_feature";
        context.style = [this](std::string name) { expect(theme::apply_style(*runtime, name, context.scope, false).has_value()); };
        h.mount("main", column({.width = Dim(width + 40), .height = Dim(844)}, {
            scroll({.id = "viewport", .width = Dim(width + 40), .height = Dim(844), .padding = Edges(20)}, {
                site::styles_feature(state, context, width)})}));
        refresh();
    }
    std::string path(std::string_view tail) const { return context.root + "/" + std::string(tail); }
    NodeId find(std::string_view tail) { return runtime->find(path(tail)); }
    bool exists(std::string_view tail) { return runtime->valid(find(tail)); }
    Rect bounds(std::string_view tail) { return runtime->screen_bounds(find(tail)); }
    void refresh() { h.settle(seconds(0.8)); site::observe_feature_examples(state, context); h.settle(seconds(0.3)); }
    void show(std::string_view tail) { runtime->scroll_into_view(find(tail)); refresh(); }
    void click(std::string_view tail) {
        show(tail); const auto id = find(tail); const auto at = runtime->screen_bounds(id).center();
        expect(hit_reaches(*runtime, id, at)) << path(tail);
        h.click(at); refresh();
    }
    void choose(std::string_view view) {
        show("comparison/view");
        const auto item = h.tree().node(path("comparison/view/") + std::string(view));
        expect(item && item->bounds.has_value()); if (!item || !item->bounds) return;
        // Segmented radios are virtual semantic items, not Runtime nodes.
        // Convert their actual semantic rectangle explicitly for real input.
        const Rect target{item->bounds->x, item->bounds->y, item->bounds->width, item->bounds->height};
        expect(contains(bounds("comparison/view"), target)) << "the radio's complete target lies inside the selector";
        expect(hit_reaches(*runtime, find("comparison/view"), target.center()));
        h.click(target.center()); refresh();
    }
    bool glass() {
        const auto glass = xrender::effects().find("liquid_glass");
        for (const auto& command : runtime->display_list().commands())
            if (const auto* layer = std::get_if<xrender::PushLayer>(&command); layer && glass && layer->effect.id == glass->id) return true;
        return false;
    }
    void controls_fit(std::string_view specimen) {
        const auto base = std::string("comparison/") + std::string(specimen);
        const auto stage = bounds(base);
        const auto heading = h.tree().node(path(base + "/controls/label"));
        expect(heading && heading->name == (language.peek() == "zh" ? specimen == "styled" ? "风格化" : "默认" : specimen == "styled" ? "Styled" : "Default"));
        for (const auto suffix : {"label", "quiet", "level", "mark", "lens"}) {
            const auto id = find(base + "/controls/" + suffix);
            expect(contains(stage, runtime->screen_bounds(id))) << std::format("{} is not clipped by the specimen", runtime->path(id));
        }
        const auto button = find(base + "/controls/mark");
        const auto name = runtime->semantic_node(button).name;
        expect(name == (language.peek() == "zh" ? "记号" : "Mark"));
        readable_label(*runtime, button, name);
        const auto label = find(base + "/controls/label");
        if (heading) readable_label(*runtime, label, heading->name, 0);
        // Both notification choices must remain separately legible targets.
        for (const auto choice : {"off", "on"}) {
            const auto id = find(base + "/controls/lens/" + choice);
            expect(runtime->valid(id)); if (!runtime->valid(id)) continue;
            const auto target = runtime->screen_bounds(id);
            expect(target.width >= 40 && target.height >= 20 && contains(stage, target));
        }
    }
};
}  // namespace

UTest cases{"site.p0_layout", [] {
     (void)themes::add_styles();
    const auto media = scratch_dir("p0-layout");

    "both home destinations follow the short introduction and are visible clickable labels before the phone scene"_test = [media] {
        for (const float width : {360.0f, 390.0f, 1280.0f}) for (const auto lang : {"en", "zh"}) {
            Headless h(HeadlessOptions{.size = {width, 844}});
            auto app = site::app("home", lang); h.runtime().set_theme(app.theme); app.setup(h.runtime());
            h.mount("main", app.view()); h.settle(seconds(0.8));
            auto& runtime = h.runtime();
            const auto scene = runtime.screen_bounds(runtime.find(std::string(home_root) + "/scene"));
            for (const auto target : {"explore", "creative"}) {
                const auto id = runtime.find(std::string(home_root) + "/intro/actions/" + target);
                expect(runtime.valid(id)); if (!runtime.valid(id)) continue;
                const auto bounds = runtime.screen_bounds(id);
                expect(contains({0, 64, width, 736}, bounds)) << "the CTA is fully above the fold without scrolling";
                if (width < 900) expect(bounds.bottom() <= scene.y + 0.5f) << "phone destinations precede all scene controls";
                const auto name = runtime.semantic_node(id).name;
                expect(name == (std::string_view(lang) == "zh" ? std::string_view(target) == "explore" ? "探索组件" : "看动效"
                                                                         : std::string_view(target) == "explore" ? "Explore components" : "See the motion"));
                readable_label(runtime, id, name);
                expect(hit_reaches(runtime, id, bounds.center())) << "the visible CTA can actually receive pointer input";
            }
            expect(h.save_png(media / std::format("home-{}-{}.png", lang, static_cast<int>(width))).has_value());
            const auto explore = std::string(home_root) + "/intro/actions/explore";
            expect(h.click(explore)); h.settle(seconds(0.5));
            const auto route = site::parse_site_route(runtime.route());
            expect(route && route->place == "components") << "the first CTA really navigates";
            expect(h.click("main/bar/mark")); h.settle(seconds(0.5));
            expect(h.click(std::string(home_root) + "/intro/actions/creative")); h.settle(seconds(0.5));
            const auto creative_route = site::parse_site_route(runtime.route());
            expect(creative_route && creative_route->place == "explore") << "the second CTA opens the real creative demos";
        }
    };

    "phone material comparison uses one full-width stage and preserves real state through pointer and keyboard switching"_test = [media] {
        for (const float width : {260.0f, 320.0f, 350.0f}) for (const auto lang : {"en", "zh"}) {
            Styles f(width, lang);
            expect(f.state.comparison_view.peek() == "styled" && f.exists("comparison/styled") && !f.exists("comparison/default"));
            expect(!f.exists("previews") && !f.exists("details")) << "additional complete samples and parameters start unmounted";
            expect(near(f.bounds("comparison/styled").width, width, 0.5));
            f.controls_fit("styled"); expect(f.glass()) << "the full-width selected material uses the real effect";
            f.click("comparison/styled/controls/quiet"); expect(!f.state.quiet.peek());
            const auto stage = f.bounds("comparison/styled");
            const auto stage_offset = stage.y - f.bounds("comparison").y;
            f.choose("default");
            expect(f.state.comparison_view.peek() == "default" && f.exists("comparison/default") && !f.exists("comparison/styled"));
            expect(!f.state.quiet.peek() && !f.runtime->semantic_node(f.find("comparison/default/controls/quiet")).states.has(State::checked));
            expect(near(f.bounds("comparison/default").width, stage.width, 0.5));
            expect(near(f.bounds("comparison/default").y - f.bounds("comparison").y, stage_offset, 0.5));
            f.controls_fit("default"); expect(!f.glass()) << "the default view does not inherit the scoped material";
            f.state.style_name.set("ink"); f.refresh();
            expect(f.state.comparison_view.peek() == "default" && !f.state.quiet.peek()) << "changing material retains the selected view and model";
            f.state.style_name.set("glass"); f.refresh();
            // Pointer selection focused the same segmented control. A real
            // arrow key returns to the material without replacing its model.
            f.h.key(Key::right); f.refresh();
            expect(f.state.comparison_view.peek() == "styled" && !f.state.quiet.peek());
            f.show("comparison/styled");
            expect(f.h.save_png(media / std::format("styles-{}-{}.png", lang, static_cast<int>(width))).has_value());
            f.choose("default");
            site::feature_examples_demo_step(f.state, f.context, "styles"); f.refresh();
            expect(f.state.quiet.peek()) << "a preview step targets the actually mounted default control";
            expect(f.runtime->valid(f.state.preview_nodes[0])) << "the visible-page coordinator has a real specimen in either view";
            f.click("actions/details");
            expect(f.exists("details/radius") && f.exists("previews/sound") && f.exists("previews/note"));
            for (const auto sample : {"sound", "note"}) {
                const auto parent = std::string("previews/") + sample;
                for (const auto child : {"heading", "quiet", "mark", "state"})
                    expect(contains(f.bounds(parent), f.bounds(parent + "/" + child))) << "disclosed samples fit their complete text and action bounds";
            }
            f.click("actions/details"); expect(!f.exists("previews") && !f.exists("details"));
        }
    };

    "desktop keeps two independent material surfaces with one shared model and no extra samples until Details"_test = [] {
        for (const auto lang : {"en", "zh"}) {
            Styles f(900, lang);
            expect(f.exists("comparison/default") && f.exists("comparison/styled") && !f.exists("comparison/view"));
            expect(!f.exists("previews") && !f.exists("details"));
            const auto plain = f.bounds("comparison/default"), styled = f.bounds("comparison/styled");
            expect(plain.right() < styled.x && std::abs(plain.y - styled.y) < 0.5f);
            f.controls_fit("default"); f.controls_fit("styled");
            f.click("comparison/default/controls/quiet");
            expect(!f.state.quiet.peek() && !f.runtime->semantic_node(f.find("comparison/styled/controls/quiet")).states.has(State::checked));
            expect(f.glass()) << "desktop still draws the scoped material effect";
        }
    };
}};
