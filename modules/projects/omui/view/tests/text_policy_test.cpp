// Public text recipes: configuration, policy overrides, motion levels and
// font-cache lifetime. Real label matching belongs to the site quality probe.
import std;
import hanim.core;
import omui.view;
import mlibs.math;
import xrender.raster;
import mlibs.testing;
import xrender.text;

using namespace omui;
using namespace mlibs::testing;

namespace {
mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection collection = [] {
        mlibs::text::FontCollection result;
        if (auto loaded = result.add_defaults(); !loaded) std::println("fonts: {}", loaded.error());
        return result;
    }();
    return collection;
}
RuntimeOptions runtime_options() {
    RuntimeOptions result; result.fonts = &fonts(); result.size = {400, 240};
    auto recipe = compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::readable_text, MotionPrimitive::fade},
                                 Transition::tween(seconds(1), hanim::ease::linear));
    recipe.text = MotionSpec::TextTransition::replace; recipe.text_rise = 1.5f;
    result.theme.motion_library["test.replace"] = recipe;
    return result;
}
Element label_element(Signal<std::string> label) {
    auto result = text({.id = "label", .text = label, .wrap = false, .size = 20, .width = Dim(320)});
    result.prop("motion", Source(Value("test.replace")));
    return result;
}
std::optional<Point> first_pen(const Runtime& runtime) {
    std::vector<Affine> transforms{Affine{}};
    for (const auto& command : runtime.display_list().commands()) {
        if (const auto* push = std::get_if<xrender::PushTransform>(&command)) transforms.push_back(transforms.back() * push->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command)) transforms.pop_back();
        else if (const auto* run = std::get_if<xrender::DrawGlyphs>(&command); run && !run->glyphs.empty()) {
            const auto style = run->styles.empty() ? xrender::GlyphStyle{} : run->styles.front();
            return transforms.back().apply({run->origin.x + run->glyphs.front().position.x + style.transform.e,
                                             run->origin.y + run->glyphs.front().position.y + style.transform.f});
        }
    }
    return std::nullopt;
}
std::uint64_t pixels(Runtime& runtime) {
    xrender::raster::Surface surface(400, 240); xrender::text::GlyphCache glyphs(runtime.fonts()); xrender::CpuRenderer renderer;
    const auto report = renderer.render(runtime.display_list(), surface, {&runtime.images(), &glyphs}, {.background = Color::white()});
    check(report.missing_glyphs == 0);
    return surface.hash();
}
struct LabelPage {
    Signal<std::string> label = signal(std::string("Before"));
    Runtime runtime;
    explicit LabelPage(MotionLevel level = MotionLevel::full, bool system_reduced = false) : runtime(runtime_options()) {
        runtime.set_motion_level(level); runtime.set_reduce_motion(system_reduced);
        runtime.mount("main", column({}, {label_element(label)}));
        runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.016), {}});
    }
    void change() { label.set("After"); runtime.frame({seconds(0.2), {}}); }
};
struct Paints { Color paper, ink; bool has_paper = false, has_ink = false; };
Paints caption_paints(Runtime& runtime) {
    const NodeId stage = runtime.find("main/stage"), caption = runtime.find("main/stage/caption");
    Paints result; std::vector<NodeId> nodes;
    for (const auto& command : runtime.display_list().commands()) {
        if (const auto* node = std::get_if<xrender::BeginNode>(&command)) nodes.push_back(runtime.node_at_index(node->node));
        else if (std::holds_alternative<xrender::EndNode>(command)) nodes.pop_back();
        else if (!nodes.empty() && nodes.back() == stage) {
            if (const auto* fill = std::get_if<xrender::FillRect>(&command)) { result.paper = fill->paint.color; result.has_paper = true; }
            if (const auto* fill = std::get_if<xrender::FillRoundRect>(&command)) { result.paper = fill->paint.color; result.has_paper = true; }
        } else if (!nodes.empty() && nodes.back() == caption)
            if (const auto* run = std::get_if<xrender::DrawGlyphs>(&command)) { result.ink = run->paint.color; result.has_ink = true; }
    }
    return result;
}
struct CaptionShape {
    std::vector<Point> points;
    std::vector<std::uint32_t> glyphs;
    bool has_nonidentity_style = false;
};
CaptionShape caption_shape(Runtime& runtime, xrender::text::GlyphCache& source) {
    const NodeId caption = runtime.find("main/stage/caption");
    CaptionShape shape;
    std::vector<NodeId> nodes;
    std::vector<Affine> transforms{Affine{}};
    for (const auto& command : runtime.display_list().commands()) {
        if (const auto* node = std::get_if<xrender::BeginNode>(&command)) nodes.push_back(runtime.node_at_index(node->node));
        else if (std::holds_alternative<xrender::EndNode>(command)) nodes.pop_back();
        else if (const auto* push = std::get_if<xrender::PushTransform>(&command)) transforms.push_back(transforms.back() * push->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command)) transforms.pop_back();
        else if (!nodes.empty() && nodes.back() == caption)
            if (const auto* run = std::get_if<xrender::DrawGlyphs>(&command)) {
                check_eq(run->paint.color.a, 1.0f);
                for (std::size_t i = 0; i < run->glyphs.size(); ++i) {
                    const auto style = run->styles.empty() ? xrender::GlyphStyle{} : run->styles[i];
                    check_eq(style.opacity, 1.0f); check_eq(style.tint.a, 0.0f);
                    shape.has_nonidentity_style = shape.has_nonidentity_style || !style.transform.identity();
                    const auto& glyph = run->glyphs[i];
                    shape.glyphs.push_back(glyph.id);
                    const auto outline = source.outline(run->font, glyph.id);
                    const Affine placed = transforms.back() * Affine::translate(run->origin.x + glyph.position.x, run->origin.y + glyph.position.y) *
                                          style.transform * Affine::scale(run->size, run->size);
                    for (const Point point : outline.points) shape.points.push_back(placed.apply(point));
                }
            }
    }
    return shape;
}
void readable(const Paints& paints) {
    check(paints.has_paper && paints.has_ink);
    if (!paints.has_paper || !paints.has_ink) return;
    const auto ink = mlibs::math::color::relative_luminance(paints.ink.r, paints.ink.g, paints.ink.b);
    const auto paper = mlibs::math::color::relative_luminance(paints.paper.r, paints.paper.g, paints.paper.b);
    check(mlibs::math::color::contrast_ratio(ink, paper) >= 4.5 - 0.00001);
}
}

int main(int argc, char** argv) {
    Suite suite{"view.text-policy"};
    suite.test("public text choices compose with drawing and preserve explicit overrides", [] {
        const auto theme = Theme::light();
        for (const auto& [name, expected] : std::array{
            std::pair{"auto", MotionSpec::TextTransition::automatic},
            std::pair{"replace", MotionSpec::TextTransition::replace},
            std::pair{"glyphs", MotionSpec::TextTransition::glyphs}}) {
            const auto policy = parse_motion(Value{}.set("use", "default.action").set("text", name), theme);
            check(policy && policy->glyphs && policy->text == expected, "public field can override a library recipe");
        }
        const auto policy = parse_motion(Value{}.set("compose", Value(Value::List{"match", "readable-text", "fade"})), theme);
        check(policy.has_value());
        if (policy) {
            check(policy->kind == MotionSpec::Kind::morph && policy->text == MotionSpec::TextTransition::automatic);
            check((policy->primitives & static_cast<std::uint16_t>(MotionPrimitive::match_drawing)) != 0);
            check((policy->primitives & static_cast<std::uint16_t>(MotionPrimitive::readable_text)) != 0);
            check((policy->primitives & static_cast<std::uint16_t>(MotionPrimitive::fade)) != 0);
        }
        const auto explicit_glyphs = parse_motion(Value{}.set("compose", Value(Value::List{"match", "readable-text"})).set("text", "glyphs"), theme);
        check(explicit_glyphs && explicit_glyphs->text == MotionSpec::TextTransition::glyphs);
        const auto legacy = parse_motion(Value{}.set("use", "default.action").set("glyphs", false), theme);
        check(legacy && !legacy->glyphs, "legacy explicit whole-run crossfade remains available");
        check(motion_primitive("readable-text") == MotionPrimitive::readable_text);
        check_eq(motion_primitive_name(MotionPrimitive::readable_text), std::string_view("readable-text"));
        for (const auto& invalid : std::array{Value("unknown"), Value(2), Value(true), Value{}, Value(Value::List{"auto"})})
            check(!parse_motion(Value{}.set("text", invalid), theme));
    });
    suite.test("ink-only contrast accepts a supported target and rejects impossible or malformed requests", [] {
        const auto theme = Theme::light();
        for (const double threshold : {0.0, 1.0, 3.0, 4.5}) {
            const auto policy = parse_motion(Value{}.set("use", "default.action").set("text_contrast", threshold), theme);
            check(policy.has_value());
            if (policy) check_eq(policy->minimum_text_contrast, threshold);
        }
        const std::array invalid{
            Value(-1.0), Value(0.5), Value(4.5001), Value(7.0),
            Value(std::numeric_limits<double>::quiet_NaN()), Value(std::numeric_limits<double>::infinity()),
            Value(-std::numeric_limits<double>::infinity()), Value("4.5"), Value(true), Value{},
            Value(Value::List{4.5}), Value{}.set("ratio", 4.5)};
        for (std::size_t i = 0; i < invalid.size(); ++i)
            check(!parse_motion(Value{}.set("text_contrast", invalid[i]), theme), std::format("invalid contrast request {} rejected", i));
    });
    suite.test("default action is readable automatically while explicit morph keeps glyph choreography", [] {
        const auto theme = Theme::light();
        const auto action = parse_motion(Value("default.action"), theme), morph = parse_motion(Value("morph"), theme);
        check(action && action->glyphs && action->text == MotionSpec::TextTransition::automatic);
        check(morph && morph->glyphs && morph->text == MotionSpec::TextTransition::glyphs);
        if (action) check_eq(action->minimum_text_contrast, 4.5);
        const auto strategies = parse_motions(Value(Value::List{
            Value{}.set("compose", Value(Value::List{"match", "readable-text"})).set("text", "replace"),
            Value{}.set("use", "simple").set("text", "glyphs").set("text_contrast", 0)}), theme);
        check(strategies && strategies->size() == 2);
        if (strategies && strategies->size() == 2) {
            check((*strategies)[0].text == MotionSpec::TextTransition::replace);
            check((*strategies)[1].text == MotionSpec::TextTransition::glyphs);
            check_eq((*strategies)[1].minimum_text_contrast, 0.0);
        }
    });
    suite.test("off and system reduced preference show the current label without creating a morph", [] {
        for (const bool system : {false, true}) {
            LabelPage page(system ? MotionLevel::full : MotionLevel::off, system);
            page.change();
            check(page.runtime.motions().empty());
            check_eq(page.runtime.get(page.runtime.find("main/label"), "text").as_string(), std::string_view("After"));
            Runtime expected(runtime_options()); expected.set_motion_level(MotionLevel::off);
            expected.mount("main", column({}, {label_element(signal(std::string("After")))}));
            expected.frame({seconds(0), {}}); expected.frame({seconds(0.016), {}});
            check_eq(pixels(page.runtime), pixels(expected));
        }
    });
    suite.test("reduced replacement really fades whole lines without vertical travel", [] {
        for (const auto level : {MotionLevel::full, MotionLevel::reduced}) {
            LabelPage page(level); const auto original = first_pen(page.runtime); check(original.has_value());
            page.change(); const auto node = page.runtime.find("main/label");
            check(page.runtime.hold_motion(node)); page.runtime.pause_motion(node);
            const auto timing = page.runtime.motion_time(node); check(timing.has_value());
            if (!timing || !original) continue;
            check(level == MotionLevel::reduced ? timing->first > 0 && timing->first <= 0.13 : timing->first >= 0.99);
            for (const double fraction : {0.25, 0.75}) {
                page.runtime.seek_motion(node, timing->first * fraction);
                page.runtime.frame({seconds(0.3 + fraction), {}});
                const auto pen = first_pen(page.runtime); check(pen.has_value());
                if (!pen) continue;
                if (level == MotionLevel::reduced) check_near(pen->y, original->y, 0.001);
                else check(std::abs(pen->y - original->y) > 1, "full-motion recipe still has real whole-line travel");
                bool faded = false;
                for (const auto& command : page.runtime.display_list().commands())
                    if (const auto* glyphs = std::get_if<xrender::DrawGlyphs>(&command)) faded = faded || (glyphs->paint.color.a > 0 && glyphs->paint.color.a < 1);
                check(faded, "reduced keeps a real finite transition instead of silently switching animation off");
            }
        }
    });
    suite.test("fonts_changed keeps the active deferred nested proof source alive", [] {
        auto config = runtime_options();
        auto theme = config.theme;
        theme.colors["surface"] = Color::white(); theme.colors["text"] = Color::black();
        auto recipe = compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::match_glyphs, MotionPrimitive::fade},
                                     Transition::tween(seconds(1), hanim::ease::linear));
        theme.motion_library["test.long"] = recipe; config.theme = theme;
        Runtime runtime(config);
        auto control = text({.id = "control", .text = "Independent nested label", .wrap = false});
        control.prop("motion", Source(Value("test.long")));
        auto stage = column({.id = "stage", .width = Dim(340), .padding = Edges(20), .gap = 40, .background = "surface"}, {
            text({.id = "caption", .text = "Persistent caption", .wrap = false}), std::move(control)});
        stage.prop("motion", Source(Value("test.long")));
        runtime.mount("main", column({}, {std::move(stage)}));
        runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.016), {}});
        theme.colors["surface"] = Color::black(); theme.colors["text"] = Color::white(); runtime.set_theme(theme);
        runtime.frame({seconds(0.2), {}});
        const auto stage_id = runtime.find("main/stage"), child_id = runtime.find("main/stage/control");
        check(runtime.hold_motion(stage_id) && runtime.hold_motion(child_id), "both outer and nested real motions are active");
        for (const auto& motion : runtime.motions()) {
            runtime.pause_motion(motion.node); runtime.seek_motion(motion.node, (motion.end - motion.start) * 0.5);
        }
        runtime.frame({seconds(0.3), {}});
        const auto before = caption_paints(runtime); readable(before);
        for (int reload = 0; reload < 3; ++reload) {
            runtime.fonts_changed(); runtime.frame({seconds(0.4 + reload * 0.1), {}});
            check(runtime.motion_time(stage_id).has_value(), "the deferred callback remains live through invalidation");
            const auto after = caption_paints(runtime); readable(after);
            check_eq(after.ink, before.ink); check_eq(after.paper, before.paper);
        }
        for (const float dpi : {2.0f, 1.0f}) {
            runtime.resize({400, 240}, dpi); runtime.frame({seconds(dpi == 2 ? 0.8 : 0.9), {}});
            check_eq(runtime.scale(), dpi);
            check(runtime.motion_time(stage_id).has_value() && runtime.motion_time(child_id).has_value(), "DPI changes preserve the active held motions");
            readable(caption_paints(runtime));
        }
    });
    suite.test("real 13 to 12 point captions stay readable on a composited translucent surface", [] {
        auto config = runtime_options();
        auto theme = config.theme;
        theme.colors["background"] = Color::white(); theme.colors["surface"] = Color::white(); theme.colors["text"] = Color::black();
        theme.text["caption"].size = 13;
        theme.motion_library["test.long"] = compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::match_glyphs, MotionPrimitive::fade},
                                                          Transition::tween(seconds(1), hanim::ease::linear));
        config.theme = theme;
        Runtime runtime(config);
        auto control = text({.id = "control", .text = "Independent nested label", .style = "caption", .wrap = false});
        control.prop("motion", Source(Value("test.long")));
        auto stage = column({.id = "stage", .width = Dim(340), .padding = Edges(20), .gap = 40, .background = "surface"}, {
            text({.id = "caption", .text = "Persistent caption", .style = "caption", .wrap = false}), std::move(control)});
        stage.prop("motion", Source(Value("test.long")));
        runtime.mount("main", column({.width = Dim(400), .height = Dim(240), .background = "background"}, {std::move(stage)}));
        runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.016), {}});
        theme.colors["surface"] = Color::hex(0x2a2763, 0.94f); theme.colors["text"] = Color::hex(0xf8fafc); theme.text["caption"].size = 12;
        runtime.set_theme(theme); runtime.frame({seconds(0.2), {}});
        const auto stage_id = runtime.find("main/stage"), child_id = runtime.find("main/stage/control");
        check(runtime.hold_motion(stage_id) && runtime.hold_motion(child_id));
        const auto motions = runtime.motions();
        for (const auto& motion : motions) runtime.pause_motion(motion.node);
        for (const double fraction : {0.25, 0.5, 0.75}) {
            for (const auto& motion : motions) runtime.seek_motion(motion.node, (motion.end - motion.start) * fraction);
            runtime.frame({seconds(0.3 + fraction), {}});
            auto actual = caption_paints(runtime);
            // The main page's actual opaque white fill is below this local
            // translucent carrier. Check the emitted ink against that
            // composition, not against the carrier's uncomposited RGB.
            actual.paper = {actual.paper.r * actual.paper.a + 1 - actual.paper.a,
                            actual.paper.g * actual.paper.a + 1 - actual.paper.a,
                            actual.paper.b * actual.paper.a + 1 - actual.paper.a, 1};
            readable(actual);
        }
        check(runtime.get(runtime.find("main/stage/caption"), "text").as_string() == "Persistent caption");
    });
    suite.test("interrupted real caption resizing keeps readable ink and continuous shaped strokes", [] {
        auto config = runtime_options(); auto theme = config.theme;
        theme.colors["background"] = Color::white(); theme.colors["surface"] = Color::white(); theme.colors["text"] = Color::black();
        theme.text["caption"].size = 13;
        theme.motion_library["test.long"] = compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::match_glyphs, MotionPrimitive::fade},
                                                          Transition::tween(seconds(1), hanim::ease::linear));
        config.theme = theme; Runtime runtime(config); xrender::text::GlyphCache outlines(runtime.fonts());
        auto control = text({.id = "control", .text = "Independent nested label", .style = "caption", .wrap = false});
        control.prop("motion", Source(Value("test.long")));
        auto stage = column({.id = "stage", .width = Dim(340), .padding = Edges(20), .gap = 40, .background = "surface"}, {
            text({.id = "caption", .text = "Persistent caption", .style = "caption", .wrap = false}), std::move(control)});
        stage.prop("motion", Source(Value("test.long")));
        runtime.mount("main", column({.width = Dim(400), .height = Dim(240), .background = "background"}, {std::move(stage)}));
        runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.016), {}});
        auto readable_composite = [&] {
            auto actual = caption_paints(runtime);
            actual.paper = {actual.paper.r * actual.paper.a + 1 - actual.paper.a,
                            actual.paper.g * actual.paper.a + 1 - actual.paper.a,
                            actual.paper.b * actual.paper.a + 1 - actual.paper.a, 1};
            readable(actual);
        };
        auto seek_all = [&](double fraction, double now) {
            const auto motions = runtime.motions(); check(!motions.empty());
            for (const auto& motion : motions) {
                check(runtime.hold_motion(motion.node)); runtime.pause_motion(motion.node);
                runtime.seek_motion(motion.node, (motion.end - motion.start) * fraction);
            }
            runtime.frame({seconds(now), {}});
        };
        theme.colors["surface"] = Color::hex(0x2a2763, 0.94f); theme.colors["text"] = Color::hex(0xf8fafc); theme.text["caption"].size = 12;
        runtime.set_theme(theme); runtime.frame({seconds(0.2), {}});
        seek_all(0.7, 0.3); readable_composite();
        const auto before = caption_shape(runtime, outlines);
        const auto before_paints = caption_paints(runtime);
        check(before.glyphs.size() > 10 && before.points.size() > 100, "real shaped Inter glyph outlines are sampled");
        check(before.has_nonidentity_style, "the retarget source really contains the in-flight size affine");
        theme.colors["surface"] = Color::white(); theme.colors["text"] = Color::black(); theme.text["caption"].size = 14;
        runtime.set_theme(theme); runtime.frame({seconds(0.4), {}});
        seek_all(0.000001, 0.5);
        const auto resumed = caption_shape(runtime, outlines);
        const auto resumed_paints = caption_paints(runtime);
        std::println("retarget source ink ({},{},{},{}) -> ({},{},{},{}); carrier ({},{},{},{}) -> ({},{},{},{})",
                     before_paints.ink.r, before_paints.ink.g, before_paints.ink.b, before_paints.ink.a,
                     resumed_paints.ink.r, resumed_paints.ink.g, resumed_paints.ink.b, resumed_paints.ink.a,
                     before_paints.paper.r, before_paints.paper.g, before_paints.paper.b, before_paints.paper.a,
                     resumed_paints.paper.r, resumed_paints.paper.g, resumed_paints.paper.b, resumed_paints.paper.a);
        check_near(resumed_paints.ink.r, before_paints.ink.r, 0.0001);
        check_near(resumed_paints.ink.g, before_paints.ink.g, 0.0001);
        check_near(resumed_paints.ink.b, before_paints.ink.b, 0.0001);
        check_near(resumed_paints.ink.a, before_paints.ink.a, 0.0001);
        check_near(resumed_paints.paper.r, before_paints.paper.r, 0.0001);
        check_near(resumed_paints.paper.g, before_paints.paper.g, 0.0001);
        check_near(resumed_paints.paper.b, before_paints.paper.b, 0.0001);
        check_near(resumed_paints.paper.a, before_paints.paper.a, 0.0001);
        check_eq(resumed.glyphs, before.glyphs); check_eq(resumed.points.size(), before.points.size());
        for (std::size_t i = 0; i < std::min(resumed.points.size(), before.points.size()); ++i) {
            check_near(resumed.points[i].x, before.points[i].x, 0.003);
            check_near(resumed.points[i].y, before.points[i].y, 0.003);
        }
        readable_composite();
        double now = 0.6;
        for (const double fraction : {0.2, 0.5, 0.8}) {
            seek_all(fraction, now); now += 0.1;
            readable_composite();
            const auto frame = caption_shape(runtime, outlines);
            check_eq(frame.glyphs, before.glyphs); check(!frame.points.empty());
            check(runtime.get(runtime.find("main/stage/caption"), "text").as_string() == "Persistent caption");
        }
    });
    suite.test("actual mixed-font Chinese receipt runs keep individually readable ink", [] {
        auto config = runtime_options(); auto theme = config.theme;
        theme.colors["background"] = Color::white(); theme.colors["surface"] = Color::white(); theme.colors["text"] = Color::black();
        theme.text["caption"].size = 13;
        theme.motion_library["test.long"] = compose_motion({MotionPrimitive::match_drawing, MotionPrimitive::match_glyphs, MotionPrimitive::fade},
                                                          Transition::tween(seconds(1), hanim::ease::linear));
        config.theme = theme; Runtime runtime(config);
        auto stage = column({.id = "stage", .width = Dim(340), .padding = Edges(20), .background = "surface"}, {
            text({.id = "caption", .text = "一份本地草稿 · 尚未发送任何内容", .style = "caption", .wrap = false})});
        stage.prop("motion", Source(Value("test.long")));
        runtime.mount("main", column({.width = Dim(400), .height = Dim(240), .background = "background"}, {std::move(stage)}));
        runtime.frame({seconds(0), {}}); runtime.frame({seconds(0.016), {}});
        const auto caption = runtime.find("main/stage/caption");
        auto check_runs = [&](std::string_view phase, bool contrast_required) {
            std::vector<NodeId> nodes; std::set<std::uint32_t> faces; std::size_t count = 0;
            const auto paper = caption_paints(runtime).paper;
            for (const auto& command : runtime.display_list().commands()) {
                if (const auto* node = std::get_if<xrender::BeginNode>(&command)) nodes.push_back(runtime.node_at_index(node->node));
                else if (std::holds_alternative<xrender::EndNode>(command)) nodes.pop_back();
                else if (!nodes.empty() && nodes.back() == caption)
                    if (const auto* run = std::get_if<xrender::DrawGlyphs>(&command); run && !run->glyphs.empty()) {
                        faces.insert(run->font.id); ++count;
                        std::println("Chinese receipt {} run{}: font={} size={} glyphs={} first=({}, {}) last=({}, {})", phase, count,
                                     run->font.id, run->size, run->glyphs.size(), run->glyphs.front().position.x, run->glyphs.front().position.y,
                                     run->glyphs.back().position.x, run->glyphs.back().position.y);
                        if (contrast_required) readable({paper, run->paint.color, true, true});
                    }
            }
            check(count >= 2 && faces.size() >= 2, "real shaping split this receipt across fallback fonts");
        };
        check_runs("before", true);
        theme.colors["surface"] = Color::black(); theme.colors["text"] = Color::white(); theme.text["caption"].size = 12;
        runtime.set_theme(theme); runtime.frame({seconds(0.2), {}});
        const auto stage_id = runtime.find("main/stage"); check(runtime.hold_motion(stage_id)); runtime.pause_motion(stage_id);
        const auto length = runtime.motion_time(stage_id); check(length.has_value()); if (!length) return;
        for (const double fraction : {0.25, 0.5, 0.75}) {
            runtime.seek_motion(stage_id, length->first * fraction); runtime.frame({seconds(0.3 + fraction), {}});
            check_runs(fraction == 0.5 ? "middle" : "transition", true);
        }
    });
    return suite.run(argc, argv);
}
