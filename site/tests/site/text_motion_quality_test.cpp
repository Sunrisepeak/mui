// Real shaped Button labels exercise the framework's typed default recipe.
// Synthetic glyph IDs alone cannot prove a readable text replacement.
import std;
import omui;
import omui.site;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

namespace {
struct Run {
    xrender::FontRef font;
    std::vector<xrender::Glyph> glyphs;
    std::vector<xrender::GlyphStyle> styles;
};
std::vector<Run> runs(Runtime& rt, NodeId node) {
    std::vector<Run> result;
    std::vector<NodeId> nodes;
    for (const auto& command : rt.display_list().commands()) {
        if (const auto* begin = std::get_if<xrender::BeginNode>(&command)) nodes.push_back(rt.node_at_index(begin->node));
        else if (std::holds_alternative<xrender::EndNode>(command)) { if (!nodes.empty()) nodes.pop_back(); }
        else if (const auto* glyphs = std::get_if<xrender::DrawGlyphs>(&command); glyphs && !nodes.empty() && nodes.back() == node && glyphs->paint.color.a > 0.001f) {
            result.push_back({glyphs->font, {glyphs->glyphs.begin(), glyphs->glyphs.end()}, {glyphs->styles.begin(), glyphs->styles.end()}});
        }
    }
    return result;
}
bool same_run(const Run& a, const Run& b) {
    if (a.font != b.font || a.glyphs.size() != b.glyphs.size()) return false;
    for (std::size_t i = 0; i < a.glyphs.size(); ++i)
        if (a.glyphs[i].id != b.glyphs[i].id || a.glyphs[i].position != b.glyphs[i].position) return false;
    return true;
}
struct ButtonScene {
    Signal<std::string> label;
    Headless h{HeadlessOptions{.size = {500, 160}}};
    NodeId button;
    explicit ButtonScene(std::string text) : label(signal(std::move(text))) {
        h.mount("main", column({.width = Dim(500), .height = Dim(160), .padding = Edges(24), .background = "background"}, {
            button_element()}));
        h.settle(); button = h.runtime().find("main/run");
    }
    Element button_element() {
        ButtonOptions options; options.id = "run"; options.text = label; options.variant = ButtonVariant::primary; options.width = Dim(360);
        return omui::button(std::move(options));
    }
    void sample(double progress) {
        auto& rt = h.runtime();
        rt.pause_motion(button);
        const auto duration = rt.motion_time(button); expect(duration.has_value());
        if (duration) rt.seek_motion(button, duration->first * progress);
        (void)h.step();
    }
};
}

static UTest cases{"site.text-motion-quality", [] {
    const auto media = scratch_dir("text-motion-quality");
    "unrelated default button captions remain coherent shaped runs through every sampled frame"_test = [media] {
        for (const auto& labels : {std::pair{"Run read → change → check", "Switch the task layout"}, std::pair{"运行读取 → 修改 → 验证", "切换任务布局"}}) {
            ButtonScene from(labels.first), target(labels.second);
            const auto before = runs(from.h.runtime(), from.button), after = runs(target.h.runtime(), target.button);
            expect(!before.empty() && !after.empty()) << "actual font shaping supplies both endpoint runs";
            from.label.set(labels.second); (void)from.h.step();
            bool intermediate = false;
            const auto first_pixels = site::frame_hash(from.h.pixels());
            for (const double t : {0.05, 0.15, 0.30, 0.45, 0.55, 0.70, 0.85, 0.95}) {
                from.sample(t);
                const auto current = runs(from.h.runtime(), from.button);
                for (const auto& run : current) {
                    const auto endpoint = [&](const Run& r) { return same_run(run, r); };
                    expect(std::ranges::any_of(before, endpoint) || std::ranges::any_of(after, endpoint)) << "no isolated old letters are scattered among a new phrase";
                    for (const auto& style : run.styles) expect(style.transform == Affine{}) << "the whole run moves; its letters keep their shaped relative positions";
                }
                intermediate = intermediate || site::frame_hash(from.h.pixels()) != first_pixels;
                from.h.record();
            }
            expect(intermediate) << "the typed default remains animated";
            const std::string lang = std::string_view(labels.first).starts_with("Run") ? "en" : "zh";
            expect(from.h.save_recording(media / ("button-replace-" + lang + ".webp"), {.delay_num = 7, .delay_den = 100}).has_value());
            expect(from.h.save_png(media / ("button-replace-" + lang + ".png")).has_value());
        }
    };
    "related edits retain glyph continuity and explicit motion remains selectable"_test = [] {
        ButtonScene scene("Save"); scene.label.set("Saved"); (void)scene.h.step();
        const auto motions = scene.h.runtime().motions();
        const auto found = std::ranges::find(motions, scene.button, &MotionState::node);
        expect(found != motions.end() && found->glyphs_kept >= 4) << "related prefixes still match actual shaped glyphs";
        scene.sample(0.4);
        const auto current = runs(scene.h.runtime(), scene.button);
        expect(std::ranges::any_of(current, [](const auto& run) { return !run.styles.empty(); }));
        const auto& recipe = scene.h.runtime().theme().motion_library.at("default.action");
        expect(recipe.text == MotionSpec::TextTransition::automatic);
        expect(scene.h.runtime().theme().motion_library.at("morph").text == MotionSpec::TextTransition::glyphs) << "explicit creative glyph matching remains available";
    };
}};
