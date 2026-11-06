// Solid-carrier protection is a public drawing policy, not component logic.
// These probes inspect the paints actually emitted at intermediate times.
import std;
import hanim.core;
import openmui.geom;
import openmui.raster;
import openmui.math;
import openmui.render.vector;
import openmui.motion;
import openmui.testing;

using namespace omui;
using namespace omui::testing;
namespace mo = omui::motion;

namespace {
// Count ordinary scalar allocations in this one executable. The regression
// compares identical warm draw sequences with/without the guard; it does not
// claim that the existing display-list/glyph pipeline makes zero allocations.
std::atomic<std::size_t> scalar_allocations{0};
}
void* operator new(std::size_t size) {
    scalar_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using geom::Color;
using geom::Rect;
using geom::Affine;
struct Frame {
    Color background = Color::white(), ink = Color::black();
    Rect carrier{0, 0, 220, 90};
    geom::CornerRadii radii{};
    geom::Point origin{20, 48};
    Affine carrier_transform{}, text_transform{};
    float text_opacity = 1, carrier_opacity = 1, glyph_opacity = 1, text_size = 16;
    Color tint = Color::transparent();
    bool no_carrier = false, gradient = false, image = false, overlay = false;
    bool effect = false, path_clip = false, narrow_clip = false, foreign = false, changed = false, stroke = false;
    int nested_count = 0;
};
// A fixed bitmap font with real, immutable coverage. No approximate em box
// is used to certify these nested cases, and concurrent reads are safe.
struct FixedGlyphs final : render::GlyphSource {
    raster::Coverage ink, blank;
    FixedGlyphs() {
        raster::PathBuilder path; path.rect({0, -12, 7, 13});
        raster::fill_coverage(path.data(), Affine{}, raster::FillRule::nonzero, geom::PixelRect{-64, -64, 128, 128}, ink);
    }
    render::GlyphMask mask(render::FontRef font, std::uint32_t glyph, float, geom::Point) override {
        if (font.id == 77) return {};
        return {glyph == 0 ? &blank : &ink, nullptr, {}};
    }
    raster::PathData outline(render::FontRef, std::uint32_t) override { return {}; }
};
struct OutlineGlyphs final : render::GlyphSource {
    raster::PathBuilder path;
    raster::Coverage ink;
    OutlineGlyphs() {
        path.rect({0, -0.75f, 0.5f, 0.75f});
        raster::fill_coverage(path.data(), Affine::scale(16, 16), raster::FillRule::nonzero, geom::PixelRect{-64, -64, 128, 128}, ink);
    }
    render::GlyphMask mask(render::FontRef, std::uint32_t, float, geom::Point) override { return {&ink, nullptr, {}}; }
    raster::PathData outline(render::FontRef, std::uint32_t) override { return path.data(); }
};
mo::Resolver names() {
    return {.keys = [](std::uint32_t marker, std::string& key, std::string&) {
        key = marker == 1 ? "main/card" : marker == 2 ? "main/card/caption" : marker >= 9 ? std::format("main/card/control{}", marker) : "main/other";
    }, .nested = [](std::uint32_t marker) { return marker >= 9; }};
}
render::DisplayList drawing(const Frame& frame) {
    render::DisplayList list;
    list.begin_node(1);
    if (frame.effect) list.push_layer(1, render::BlendMode::normal, render::EffectRef{77}, {}, frame.carrier);
    if (frame.path_clip) {
        render::Path path; path.move_to({0, 0}); path.line_to({220, 0}); path.line_to({220, 90}); path.line_to({0, 90}); path.close();
        list.push_clip(path);
    } else if (frame.narrow_clip) list.push_clip(Rect{0, 0, 12, 12});
    if (frame.foreign) list.begin_node(3);
    if (!frame.no_carrier) {
        list.push_transform(frame.carrier_transform);
        auto paint = render::Paint::solid(frame.background);
        if (frame.gradient) paint.kind = render::Paint::Kind::gradient;
        list.push_layer(frame.carrier_opacity);
        list.fill_round_rect(frame.carrier, frame.radii, paint);
        list.pop_layer(); list.pop_transform();
    }
    if (frame.stroke) list.stroke_round_rect(frame.carrier, frame.radii, 2, render::Paint::solid(Color::hex(0x64748b)));
    if (frame.foreign) list.end_node();
    if (frame.image) list.draw_image(render::ImageRef{1}, frame.carrier, frame.carrier);
    if (frame.overlay) list.fill_rect(frame.carrier, Color::hex(0x808080, 0.25f));
    list.begin_node(2);
    list.push_transform(frame.text_transform);
    list.push_layer(frame.text_opacity);
    const std::array<render::Glyph, 3> glyphs{{
        {.id = 11, .position = {0, 0}, .cluster = 0},
        {.id = 12, .position = {10, 0}, .cluster = 1},
        {.id = frame.changed ? 19u : 13u, .position = {20, 0}, .cluster = 2}}};
    const std::array<render::GlyphStyle, 3> styles{{
        {.opacity = frame.glyph_opacity, .tint = frame.tint},
        {.opacity = frame.glyph_opacity, .tint = frame.tint},
        {.opacity = frame.glyph_opacity, .tint = frame.tint}}};
    list.draw_glyphs(render::FontRef{1}, frame.text_size, frame.origin, glyphs, render::Paint::solid(frame.ink), styles);
    list.pop_layer(); list.pop_transform(); list.end_node();
    for (int i = 0; i < frame.nested_count; ++i) {
        list.begin_node(static_cast<std::uint32_t>(9 + i));
        list.fill_rect({140, 65, 20, 10}, Color::hex(0x34d399));
        list.end_node();
    }
    if (frame.narrow_clip || frame.path_clip) list.pop_clip();
    if (frame.effect) list.pop_layer();
    list.end_node();
    return list;
}
mo::Options options(double threshold = 4.5) {
    mo::Options o;
    o.curve.kind = mo::Curve::Kind::tween; o.curve.duration = 1; o.curve.ease = hanim::ease::linear;
    o.minimum_text_contrast = threshold;
    return o;
}
mo::Morph transition(const Frame& a, const Frame& b, mo::Options o = options()) {
    const auto from = drawing(a), to = drawing(b);
    return mo::Morph(mo::Matter::extract(from, names()), mo::Matter::extract(to, names()), std::move(o), 0);
}
render::DisplayList at(const mo::Morph& motion, double time) {
    render::DisplayList list; motion.draw(time, list); return list;
}
struct Paints { Color background, ink; bool fill = false, text = false; };
Paints paints(const render::DisplayList& list) {
    Paints result;
    for (const auto& command : list.commands()) {
        if (const auto* fill = std::get_if<render::FillRect>(&command)) { result.background = fill->paint.color; result.fill = true; }
        else if (const auto* fill = std::get_if<render::FillRoundRect>(&command)) { result.background = fill->paint.color; result.fill = true; }
        else if (const auto* glyphs = std::get_if<render::DrawGlyphs>(&command)) { result.ink = glyphs->paint.color; result.text = true; }
    }
    return result;
}
double ratio(Color a, Color b) {
    return math::color::contrast_ratio(math::color::relative_luminance(a.r, a.g, a.b), math::color::relative_luminance(b.r, b.g, b.b));
}
void readable(const mo::Morph& motion, double time) {
    const auto frame = paints(at(motion, time));
    check(frame.fill && frame.text, "real carrier and unchanged glyph drawing emitted");
    if (frame.fill && frame.text) {
        check(ratio(frame.ink, frame.background) >= 4.5 - 0.00001, std::format("contrast {} at {}", ratio(frame.ink, frame.background), time));
        check_eq(frame.ink.a, 1.0f);
    }
}
Paints caption_paints(const render::DisplayList& list) {
    Paints result;
    std::vector<std::uint32_t> nodes;
    for (const auto& command : list.commands()) {
        if (const auto* node = std::get_if<render::BeginNode>(&command)) nodes.push_back(node->node);
        else if (std::holds_alternative<render::EndNode>(command)) nodes.pop_back();
        else if (!result.fill) {
            if (const auto* fill = std::get_if<render::FillRect>(&command)) { result.background = fill->paint.color; result.fill = true; }
            else if (const auto* fill = std::get_if<render::FillRoundRect>(&command)) { result.background = fill->paint.color; result.fill = true; }
        }
        if (!result.text && !nodes.empty() && nodes.back() == 2)
            if (const auto* glyphs = std::get_if<render::DrawGlyphs>(&command)) { result.ink = glyphs->paint.color; result.text = true; }
    }
    return result;
}
Color over(Color surface, Color under) {
    return {surface.r * surface.a + under.r * (1 - surface.a), surface.g * surface.a + under.g * (1 - surface.a),
            surface.b * surface.a + under.b * (1 - surface.a), 1};
}
Color actual_carrier(const render::DisplayList& list) {
    Color color; std::vector<std::uint32_t> nodes;
    for (const auto& command : list.commands()) {
        if (const auto* node = std::get_if<render::BeginNode>(&command)) nodes.push_back(node->node);
        else if (std::holds_alternative<render::EndNode>(command)) nodes.pop_back();
        else if (!nodes.empty() && nodes.back() == 1) {
            if (const auto* fill = std::get_if<render::FillRect>(&command)) color = fill->paint.color;
            else if (const auto* fill = std::get_if<render::FillRoundRect>(&command)) color = fill->paint.color;
        }
    }
    return color;
}
void near(Color actual, Color expected) {
    check_near(actual.r, expected.r, 0.0001); check_near(actual.g, expected.g, 0.0001);
    check_near(actual.b, expected.b, 0.0001); check_near(actual.a, expected.a, 0.0001);
}
}

int main(int argc, char** argv) {
    Suite suite{"motion.text-contrast"};
    const Frame light{};
    Frame dark; dark.background = Color::black(); dark.ink = Color::white();
    suite.test("opposite ink and carrier polarities stay readable through real middle paints", [&] {
        auto guarded = transition(light, dark);
        check_eq(guarded.stats().text_contrast_runs, std::size_t{1});
        for (int step = 0; step <= 100; ++step) readable(guarded, static_cast<double>(step) / 100);
        near(paints(at(guarded, 0)).ink, light.ink); near(paints(at(guarded, 0)).background, light.background);
        near(paints(at(guarded, 1)).ink, dark.ink); near(paints(at(guarded, 1)).background, dark.background);
        auto unguarded = transition(light, dark, options(0));
        const auto middle = paints(at(unguarded, 0.5));
        check(ratio(middle.ink, middle.background) < 1.01, "the failure probe really has coincident mixed inks/backgrounds");
        check_eq(unguarded.stats().text_contrast_runs, std::size_t{0});
        auto reverse = transition(dark, light);
        for (int step = 0; step <= 100; ++step) readable(reverse, static_cast<double>(step) / 100);
    });
    suite.test("carrier uses its own staggered curve instead of the text progress", [&] {
        auto timing = options(); timing.order = mo::Order::document; timing.stagger = 0.3;
        auto motion = transition(light, dark, timing);
        check_eq(motion.stats().text_contrast_runs, std::size_t{1});
        for (int step = 0; step <= 130; ++step) readable(motion, static_cast<double>(step) / 100);
        check_near(motion.end(), 1.3, 0.00001);
    });
    suite.test("transparent, disabled, changed and intentionally low-contrast drawings are not normalized", [&] {
        for (int variant = 0; variant < 7; ++variant) {
            Frame a = light, b = dark;
            if (variant == 0) a.ink.a = b.ink.a = 0.5f;
            if (variant == 1) a.text_opacity = b.text_opacity = 0.5f;
            if (variant == 2) a.glyph_opacity = b.glyph_opacity = 0.5f;
            if (variant == 3) a.carrier_opacity = b.carrier_opacity = 0.5f;
            if (variant == 4) a.background.a = b.background.a = 0.5f;
            if (variant == 5) b.changed = true;
            if (variant == 6) a.ink = Color::hex(0xb0b0b0);
            auto guarded = transition(a, b);
            auto baseline = transition(a, b, options(0));
            check_eq(guarded.stats().text_contrast_runs, std::size_t{0});
            for (const double time : {0.1, 0.5, 0.9}) check_eq(at(guarded, time).hash(), at(baseline, time).hash());
        }
    });
    suite.test("images, effects, gradients, foreign fills and unprovable clips are not carriers", [&] {
        for (int variant = 0; variant < 9; ++variant) {
            Frame a = light, b = dark;
            if (variant == 0) a.no_carrier = b.no_carrier = true;
            if (variant == 1) a.image = b.image = true;
            if (variant == 2) a.overlay = b.overlay = true;
            if (variant == 3) a.gradient = b.gradient = true;
            if (variant == 4) a.effect = b.effect = true;
            if (variant == 5) a.path_clip = b.path_clip = true;
            if (variant == 6) a.narrow_clip = b.narrow_clip = true;
            if (variant == 7) a.foreign = b.foreign = true;
            if (variant == 8) a.tint = b.tint = Color::hex(0x607080, 0.2f);
            auto guarded = transition(a, b);
            check_eq(guarded.stats().text_contrast_runs, std::size_t{0});
        }
    });
    suite.test("rounded and moving carriers are checked in their actual geometry", [&] {
        Frame a = light, b = dark;
        a.radii = geom::CornerRadii::all(12); b.radii = geom::CornerRadii::all(18);
        a.stroke = b.stroke = true;
        b.carrier.width = 250; b.carrier.height = 100;
        auto rounded = transition(a, b);
        check_eq(rounded.stats().text_contrast_runs, std::size_t{1});
        for (int step = 0; step <= 100; ++step) readable(rounded, static_cast<double>(step) / 100);
        a.carrier = b.carrier = {0, 0, 60, 60}; a.radii = b.radii = geom::CornerRadii::all(30);
        a.origin = b.origin = {1, 21};
        auto corner = transition(a, b);
        check_eq(corner.stats().text_contrast_runs, std::size_t{0});
        a = light; b = dark;
        b.carrier_transform = b.text_transform = Affine::translate(200, 0);
        auto timing = options(); timing.order = mo::Order::document; timing.stagger = 0.8;
        auto moving = transition(a, b, timing);
        auto baseline = transition(a, b, [&] { auto o = timing; o.minimum_text_contrast = 0; return o; }());
        check_eq(moving.stats().text_contrast_runs, std::size_t{1});
        // At .5 the carrier moved while the delayed text did not. Its
        // endpoint association cannot justify pretending it is underneath.
        check_eq(at(moving, 0.5).hash(), at(baseline, 0.5).hash());
    });
    suite.test("guarded paint is seekable and survives an interrupted solid-carrier snapshot", [&] {
        auto first = transition(light, dark);
        const auto saved = at(first, 0.63).hash();
        (void)at(first, 0.2); (void)at(first, 0.9);
        check_eq(at(first, 0.63).hash(), saved);
        const auto before = paints(at(first, 0.63));
        const auto target = drawing(light);
        mo::Morph second(first.snapshot(0.63), mo::Matter::extract(target, names()), options(), 0);
        check_eq(second.stats().text_contrast_runs, std::size_t{1});
        const auto resumed = paints(at(second, 0.000001));
        near(resumed.ink, before.ink); near(resumed.background, before.background);
        for (int step = 0; step <= 100; ++step) readable(second, static_cast<double>(step) / 100);
    });
    suite.test("guard adds no scalar heap allocations to an identical warm draw sequence", [&] {
        auto guarded = transition(light, dark), baseline = transition(light, dark, options(0));
        render::DisplayList protected_list, plain_list;
        protected_list.reserve(64); plain_list.reserve(64);
        guarded.draw(0.5, protected_list); baseline.draw(0.5, plain_list);
        auto count = [](const mo::Morph& motion, render::DisplayList& list) {
            const auto before = scalar_allocations.load(std::memory_order_relaxed);
            for (int step = 1; step < 100; ++step) {
                list.clear(); motion.draw(static_cast<double>(step) / 100, list);
            }
            return scalar_allocations.load(std::memory_order_relaxed) - before;
        };
        const auto plain_allocations = count(baseline, plain_list);
        const auto protected_allocations = count(guarded, protected_list);
        check(plain_allocations > 0, "instrumentation sees the existing glyph style allocation");
        check_eq(protected_allocations, plain_allocations);
    });
    suite.test("direct options expose the actual supported ink-only threshold", [&] {
        auto clamped = transition(light, dark, options(7));
        check_eq(clamped.options().minimum_text_contrast, 4.5);
        auto negative = transition(light, dark, options(-1));
        check_eq(negative.options().minimum_text_contrast, 0.0);
        auto not_finite = transition(light, dark, options(std::numeric_limits<double>::quiet_NaN()));
        check_eq(not_finite.options().minimum_text_contrast, 0.0);
    });
    suite.test("independently moving nested drawings are proved from this draw exactly once", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy);
        auto raw = transition(a, b, options(0));
        check_eq(motion.stats().text_contrast_runs, std::size_t{1});
        for (const double time : {0.39, 0.5, 0.61}) {
            int calls = 0;
            const bool crossing = time == 0.5;
            auto nested = [&](std::uint32_t marker, render::DisplayList& out) {
                ++calls; out.begin_node(marker);
                out.fill_rect(crossing ? Rect{20, 35, 28, 15} : Rect{140, 65, 20, 10}, Color::hex(0x34d399));
                out.end_node();
            };
            render::DisplayList output; motion.draw(time, output, nested);
            check_eq(calls, 1); check(output.balanced());
            const auto frame = caption_paints(output);
            if (crossing) near(frame.ink, caption_paints(at(raw, time)).ink);
            else check(ratio(frame.ink, frame.background) >= 4.5 - 0.00001, "far-away actual nested drawing permits protection");
        }
        auto missing_source = transition(a, b);
        check_eq(missing_source.stats().text_contrast_runs, std::size_t{0});
    });
    suite.test("unknown nested effects and unsupported real glyph bounds cannot certify clear space", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy), raw = transition(a, b, options(0));
        const auto expected = caption_paints(at(raw, 0.5)).ink;
        for (int variant = 0; variant < 3; ++variant) {
            int calls = 0;
            render::DisplayList output;
            motion.draw(0.5, output, [&](std::uint32_t marker, render::DisplayList& out) {
                ++calls; out.begin_node(marker);
                if (variant == 0) out.push_layer(1, render::BlendMode::normal, render::EffectRef{77}, {}, {140, 65, 20, 10});
                if (variant == 0) out.fill_rect({140, 65, 20, 10}, Color::black());
                else {
                    const std::array<render::Glyph, 1> text{{{.id = 11}}};
                    const std::array<render::GlyphStyle, 1> styles{{{.transform = variant == 2 ? Affine::rotation(0.8f, 0.6f) : Affine{}}}};
                    out.draw_glyphs(render::FontRef{variant == 1 ? 77u : 1u}, 16, {140, 75}, text, render::Paint::solid(Color::black()), styles);
                }
                if (variant == 0) out.pop_layer();
                out.end_node();
            });
            check_eq(calls, 1); check(output.balanced());
            near(caption_paints(output).ink, expected);
        }
    });
    suite.test("actual translated styled glyph masks are bounded without em approximations", [] {
        FixedGlyphs source;
        const std::array<render::Glyph, 1> glyphs{{{.id = 11}}};
        const std::array<render::GlyphStyle, 1> translated{{{.transform = Affine::translate(40, 3)}}};
        render::DisplayList list;
        list.draw_glyphs(render::FontRef{1}, 16, {20, 48}, glyphs, render::Paint::solid(Color::black()), translated);
        const auto& run = std::get<render::DrawGlyphs>(list.commands().front());
        const auto bounds = mo::glyph_ink_bounds(run, Affine{}, source);
        check(bounds.has_value());
        if (bounds) { check_near(bounds->x, 59, 0.001); check_near(bounds->y, 38, 0.001); check_near(bounds->width, 9, 0.001); check_near(bounds->height, 15, 0.001); }
        check(!mo::glyph_ink_bounds(run, Affine::rotation(0.8f, 0.6f), source));
        check(!mo::glyph_ink_bounds(run, Affine::scale(1, 2), source));
        const std::array<render::Glyph, 1> blank{{{.id = 0}}};
        render::DisplayList blank_list;
        blank_list.draw_glyphs(render::FontRef{1}, 16, {}, blank, render::Paint::solid(Color::black()));
        const auto empty = mo::glyph_ink_bounds(std::get<render::DrawGlyphs>(blank_list.commands().front()), Affine{}, source);
        check(empty && empty->empty(), "known blank mask is distinct from missing geometry");
    });
    suite.test(threads ? "nested draw reentry and concurrent seeking keep independent proof scratch"
                       : "nested draw reentry and threadless seeking keep independent proof scratch", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto outer = transition(a, b, policy), inner = transition(a, b, policy);
        auto draw_once = [&] {
            render::DisplayList output;
            outer.draw(0.5, output, [&](std::uint32_t marker, render::DisplayList& into) {
                into.begin_node(marker); into.push_transform(Affine::translate(300, 0));
                inner.draw(0.5, into, [](std::uint32_t child, render::DisplayList& nested) {
                    nested.begin_node(child); nested.fill_rect({140, 65, 20, 10}, Color::black()); nested.end_node();
                });
                into.pop_transform(); into.end_node();
            });
            return output;
        };
        const auto expected = draw_once();
        const auto frame = caption_paints(expected);
        check(ratio(frame.ink, frame.background) >= 4.5 - 0.00001);
        if constexpr (threads) {
            auto first = std::async(std::launch::async, [&] { return draw_once().hash(); });
            auto second = std::async(std::launch::async, [&] { return draw_once().hash(); });
            check_eq(first.get(), expected.hash()); check_eq(second.get(), expected.hash());
        } else {
            check_eq(draw_once().hash(), expected.hash()); check_eq(draw_once().hash(), expected.hash());
        }
    });
    suite.test("proof capacity overflow preserves drawing and conservatively skips ink correction", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.nested_count = b.nested_count = 65;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy), raw = transition(a, b, options(0));
        int calls = 0;
        render::DisplayList output;
        motion.draw(0.5, output, [&](std::uint32_t marker, render::DisplayList& nested) {
            ++calls; nested.begin_node(marker); nested.fill_rect({140, 65, 20, 10}, Color::black()); nested.end_node();
        });
        check_eq(calls, 65); check(output.balanced());
        near(caption_paints(output).ink, caption_paints(at(raw, 0.5)).ink);
    });
    suite.test("deep nested proof exhaustion skips safely and releases every lease", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy);
        render::DisplayList output;
        std::function<void(int, render::DisplayList&)> draw_level;
        draw_level = [&](int depth, render::DisplayList& into) {
            motion.draw(0.5, into, [&](std::uint32_t marker, render::DisplayList& nested) {
                nested.begin_node(marker); nested.push_transform(Affine::translate(300, 0));
                if (depth < 9) draw_level(depth + 1, nested);
                else nested.fill_rect({140, 65, 20, 10}, Color::black());
                nested.pop_transform(); nested.end_node();
            });
        };
        draw_level(0, output);
        check(output.balanced());
        const auto background = caption_paints(output).background;
        std::size_t texts = 0;
        for (const auto& command : output.commands())
            if (const auto* text = std::get_if<render::DrawGlyphs>(&command)) {
                if (texts < 8) check(ratio(text->paint.color, background) >= 4.5 - 0.00001, "eight independent leases retain their own patches");
                else check(ratio(text->paint.color, background) < 1.01, "overflow draws honestly without claiming protection");
                ++texts;
            }
        check_eq(texts, std::size_t{10});
        render::DisplayList again;
        motion.draw(0.5, again, [](std::uint32_t marker, render::DisplayList& nested) {
            nested.begin_node(marker); nested.fill_rect({140, 65, 20, 10}, Color::black()); nested.end_node();
        });
        const auto frame = caption_paints(again);
        check(ratio(frame.ink, frame.background) >= 4.5 - 0.00001, "all recursive leases returned before the next seek");
    });
    suite.test("safe translucent carriers use the actual opaque prefix instead of a presumed white backdrop", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.background.a = b.background.a = 0.94f;
        for (const float scale : {1.0f, 2.0f}) for (const auto under : {Color::white(), Color::black(), Color::hex(0x23395b), Color::hex(0xf0a060)}) {
            auto policy = options(); policy.proof_scale = scale;
            policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
            auto motion = transition(a, b, policy);
            check_eq(motion.stats().text_contrast_runs, std::size_t{1});
            for (int step = 0; step <= 100; step += 2) {
                render::DisplayList output;
                output.push_transform(Affine::translate(37, 19) * Affine::scale(1.25f, 1.25f));
                output.fill_rect({-500, -500, 1000, 1000}, under);
                motion.draw(static_cast<double>(step) / 100, output); output.pop_transform();
                const auto ink = caption_paints(output).ink;
                const auto surface = actual_carrier(output);
                check(ratio(ink, over(surface, under)) >= 4.5 - 0.00001,
                      std::format("actual alpha-composite contrast at {} scale {}", step, scale));
                check_near(surface.a, 0.94, 0.00001);
            }
        }
    });
    suite.test("translucent proof rejects absent opaque underpaint and partial or unknown composition", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.background.a = b.background.a = 0.94f;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy), raw = transition(a, b, options(0));
        const auto expected = caption_paints(at(raw, 0.5)).ink;
        for (int variant = 0; variant < 7; ++variant) {
            render::DisplayList output;
            if (variant) output.fill_rect({-500, -500, 1000, 1000}, Color::white());
            if (variant == 1) output.draw_image(render::ImageRef{1}, {0, 0, 220, 90}, {0, 0, 220, 90});
            if (variant == 2) {
                render::Paint gradient; gradient.kind = render::Paint::Kind::gradient;
                output.fill_rect({0, 0, 220, 90}, gradient);
            }
            if (variant == 3) output.fill_rect({20, 0, 12, 90}, Color::black());
            if (variant == 4) output.push_clip(Rect{0, 0, 30, 240});
            if (variant == 5) output.push_layer(0.5f);
            if (variant == 6) output.push_layer(1, render::BlendMode::normal, render::EffectRef{999999}, {}, {0, 0, 220, 90});
            motion.draw(0.5, output);
            if (variant == 4) output.pop_clip();
            if (variant >= 5) output.pop_layer();
            check(output.balanced()); near(caption_paints(output).ink, expected);
        }
        // Opaque parent recovery is valid even when a still earlier image
        // was unknown: the later solid really covers every selected pixel.
        render::DisplayList recovered;
        recovered.draw_image(render::ImageRef{1}, {0, 0, 220, 90}, {0, 0, 220, 90});
        recovered.fill_rect({-500, -500, 1000, 1000}, Color::hex(0x23395b));
        motion.draw(0.5, recovered);
        check(ratio(caption_paints(recovered).ink, over(actual_carrier(recovered), Color::hex(0x23395b))) >= 4.5 - 0.00001);
    });
    suite.test("registered effect write bounds preserve source coverage and reject actual overlap", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.background.a = b.background.a = 0.94f; a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy), raw = transition(a, b, options(0));
        const auto effect = render::effects().find("liquid_glass"); check(effect.has_value()); if (!effect) return;
        const auto expected = caption_paints(at(raw, 0.5)).ink;
        auto params = render::effects().params(*effect, {});
        // Large sampling reach is not the effect's generated write region.
        params[0] = 100; params[10] = 0;
        for (int variant = 0; variant < 3; ++variant) {
            render::DisplayList output; output.fill_rect({-500, -500, 1000, 1000}, Color::white());
            int calls = 0;
            motion.draw(0.5, output, [&](std::uint32_t marker, render::DisplayList& nested) {
                ++calls; nested.begin_node(marker);
                const Rect declared = variant == 1 ? Rect{20, 35, 28, 15} : Rect{140, 65, 20, 10};
                nested.push_layer(1, render::BlendMode::normal, *effect, params, declared);
                // Even when declared bounds are far away, source content
                // outside them remains part of the actual write envelope.
                nested.fill_rect(variant == 2 ? Rect{20, 35, 28, 15} : declared, Color::black());
                nested.pop_layer(); nested.end_node();
            });
            check_eq(calls, 1); check(output.balanced());
            if (variant == 0) check(ratio(caption_paints(output).ink, over(actual_carrier(output), Color::white())) >= 4.5 - 0.00001);
            else near(caption_paints(output).ink, expected);
        }
    });
    suite.test("live proof scale is read once and invalid scale never certifies a correction", [&] {
        FixedGlyphs glyphs;
        Frame a = light, b = dark; a.background.a = b.background.a = 0.94f;
        float scale = 1; int reads = 0; float greatest_matrix = 0;
        auto policy = options(); policy.proof_scale = 1;
        policy.current_proof_scale = [&] { ++reads; return scale; };
        policy.glyph_bounds = [&](const render::DrawGlyphs& run, const Affine& transform) {
            greatest_matrix = std::max(greatest_matrix, transform.a);
            return mo::glyph_ink_bounds(run, transform, glyphs);
        };
        auto motion = transition(a, b, policy), raw = transition(a, b, options(0));
        for (const float dpi : {1.0f, 2.0f, 1.0f}) {
            scale = dpi; reads = 0; greatest_matrix = 0;
            render::DisplayList output; output.fill_rect({-500, -500, 1000, 1000}, Color::white()); motion.draw(0.5, output);
            check_eq(reads, 1); check_near(greatest_matrix, dpi, 0.0001);
            check(ratio(caption_paints(output).ink, over(actual_carrier(output), Color::white())) >= 4.5 - 0.00001);
        }
        for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            scale = invalid; reads = 0;
            render::DisplayList output; output.fill_rect({-500, -500, 1000, 1000}, Color::white()); motion.draw(0.5, output);
            check_eq(reads, 1); near(caption_paints(output).ink, caption_paints(at(raw, 0.5)).ink);
        }
    });
    suite.test("styled vector glyph bounds use the actual outline and complete affine", [] {
        OutlineGlyphs source;
        const std::array<render::Glyph, 1> glyphs{{{.id = 11}}};
        const Affine device = Affine::translate(7, 11) * Affine::scale(2, 2);
        for (const bool rotated : {false, true}) {
            const std::array<render::GlyphStyle, 1> styles{{{.transform = rotated
                ? Affine{0, 1, -1, 0, 4, 3} : Affine::translate(4, 3) * Affine::scale(1.5f, 0.5f)}}};
            render::DisplayList drawing;
            drawing.draw_glyphs(render::FontRef{1}, 16, {20, 48}, glyphs, render::Paint::solid(Color::black()), styles);
            const auto bounds = mo::glyph_ink_bounds(std::get<render::DrawGlyphs>(drawing.commands().front()), device, source);
            check(bounds.has_value());
            if (bounds) {
                check_near(bounds->x, 54, 0.0001); check_near(bounds->y, rotated ? 112 : 100, 0.0001);
                check_near(bounds->width, 26, 0.0001); check_near(bounds->height, rotated ? 18 : 14, 0.0001);
            }
        }
    });
    suite.test("unchanged content can keep contrast while its font size changes", [&] {
        OutlineGlyphs glyphs;
        Frame a = light, b = dark; a.text_size = 13; b.text_size = 12; a.background.a = b.background.a = 0.94f;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy);
        check_eq(motion.stats().text_contrast_runs, std::size_t{1});
        for (const double time : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            render::DisplayList output; output.fill_rect({-500, -500, 1000, 1000}, Color::white()); motion.draw(time, output);
            check(ratio(caption_paints(output).ink, over(actual_carrier(output), Color::white())) >= 4.5 - 0.00001);
        }
    });
    suite.test("contextual snapshots retain sampled protected ink without importing the page prefix", [&] {
        OutlineGlyphs glyphs;
        Frame a = light, b = dark; a.text_size = 13; b.text_size = 12; a.background.a = b.background.a = 0.94f;
        a.nested_count = b.nested_count = 1;
        auto policy = options();
        policy.glyph_bounds = [&glyphs](const render::DrawGlyphs& run, const Affine& transform) { return mo::glyph_ink_bounds(run, transform, glyphs); };
        auto motion = transition(a, b, policy);
        render::DisplayList prefix;
        prefix.begin_node(3); prefix.push_transform(Affine::translate(37, 19)); prefix.push_clip(Rect{-500, -500, 1000, 1000});
        prefix.fill_rect({-500, -500, 1000, 1000}, Color::white());
        int calls = 0;
        const auto nested = [&](std::uint32_t marker, render::DisplayList& out) {
            ++calls; out.begin_node(marker); out.fill_rect({140, 65, 20, 10}, Color::black()); out.end_node();
        };
        const auto screen = [&](double time) {
            render::DisplayList out;
            for (const auto& command : prefix.commands()) out.append_command(command);
            motion.draw(time, out, nested); out.pop_clip(); out.pop_transform(); out.end_node();
            return out;
        };
        const auto earlier = screen(0.19); // sampling must not replay this last drawing
        calls = 0;
        const auto expected = screen(0.63); check_eq(calls, 1);
        calls = 0;
        auto snapshot = motion.snapshot(0.63, nested, mo::SnapshotContext{prefix.commands()});
        check_eq(calls, 1); check(snapshot.list().balanced());
        const auto ink = caption_paints(snapshot.list()).ink;
        near(ink, caption_paints(expected).ink);
        check(ink != caption_paints(earlier).ink, "the root is evaluated at the requested time");
        check(ratio(ink, over(actual_carrier(snapshot.list()), Color::white())) >= 4.5 - 0.00001);
        check(std::ranges::none_of(snapshot.nodes(), [](const auto& node) { return node.key == "main/other"; }), "prefix owner is proof-only");
        check(snapshot.clips().empty(), "ancestor prefix clip is not imported into the returned local Matter");
        bool local_carrier = false;
        for (const auto& piece : snapshot.pieces()) if (piece.cls == mo::PieceClass::fill && snapshot.nodes()[piece.node].key == "main/card") {
            check_eq(piece.transform, Affine{}); local_carrier = true;
        }
        check(local_carrier, "the borrowed prefix transform does not shift local geometry");
        const auto own_hash = snapshot.hash(); prefix.clear(); check_eq(snapshot.hash(), own_hash);
        const auto isolated = motion.snapshot(0.63, nested);
        check(ratio(caption_paints(isolated.list()).ink, over(actual_carrier(isolated.list()), Color::white())) < 4.5,
              "the compatible isolated API does not invent an opaque page backdrop");
        render::DisplayList unknown;
        unknown.draw_image(render::ImageRef{1}, {0, 0, 220, 90}, {0, 0, 220, 90});
        const auto unproved = motion.snapshot(0.63, nested, mo::SnapshotContext{unknown.commands()});
        near(caption_paints(unproved.list()).ink, caption_paints(isolated.list()).ink);
    });
    return suite.run(argc, argv);
}
