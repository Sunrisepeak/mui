// Text transitions preserve the drawing that an interrupted motion actually
// showed. These public-Morph probes need no text engine or component clock.
import std;
import hanim.core;
import mlibs.geom;
import xrender.vector;
import omui.motion;
import mlibs.testing;

using namespace omui;
using namespace mlibs::testing;
namespace mo = omui::motion;

namespace {
mo::Resolver resolver() {
    return {.keys = [](std::uint32_t, std::string& key, std::string&) { key = "main/run"; }};
}
mo::Options linear() {
    mo::Options o;
    o.curve.kind = mo::Curve::Kind::tween;
    o.curve.duration = 1;
    o.curve.ease = hanim::ease::linear;
    return o;
}
std::vector<xrender::Glyph> glyphs(std::initializer_list<std::uint32_t> ids) {
    std::vector<xrender::Glyph> result;
    for (const auto id : ids) {
        const auto n = static_cast<std::uint32_t>(result.size());
        result.push_back({.id = id, .position = {static_cast<float>(n) * 10, 0}, .cluster = n, .grapheme = n});
    }
    return result;
}
xrender::DisplayList drawing(std::span<const xrender::Glyph> g, xrender::GlyphStyle style = {},
                            mlibs::geom::Affine parent = {}, float size = 20, mlibs::geom::Point origin = {20, 40}) {
    xrender::DisplayList list;
    list.begin_node(1);
    list.push_transform(parent);
    std::vector<xrender::GlyphStyle> styles(g.size(), style);
    list.draw_glyphs(xrender::FontRef{1}, size, origin, g, xrender::Paint::solid(mlibs::geom::Color::hex(0x182030)), styles);
    list.pop_transform();
    list.end_node();
    return list;
}
mo::Morph transition(xrender::DisplayList a, xrender::DisplayList b, mo::Options options = linear()) {
    return mo::Morph(mo::Matter::extract(a, resolver()), mo::Matter::extract(b, resolver()), std::move(options), 0);
}
struct VisibleGlyph {
    std::uint32_t id;
    mlibs::geom::Affine outline;
    float opacity;
    mlibs::geom::Color tint;
};
std::vector<VisibleGlyph> visible(const xrender::DisplayList& list) {
    std::vector<VisibleGlyph> result;
    std::vector<mlibs::geom::Affine> transforms{mlibs::geom::Affine{}};
    for (const auto& command : list.commands()) {
        if (const auto* p = std::get_if<xrender::PushTransform>(&command)) transforms.push_back(transforms.back() * p->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command)) transforms.pop_back();
        else if (const auto* g = std::get_if<xrender::DrawGlyphs>(&command)) {
            for (std::size_t i = 0; i < g->glyphs.size(); ++i) {
                const auto style = g->styles.empty() ? xrender::GlyphStyle{} : g->styles[i];
                const auto pen = mlibs::geom::Point{g->origin.x + g->glyphs[i].position.x, g->origin.y + g->glyphs[i].position.y};
                result.push_back({g->glyphs[i].id,
                                  transforms.back() * mlibs::geom::Affine::translate(pen.x, pen.y) * style.transform * mlibs::geom::Affine::scale(g->size, g->size),
                                  g->paint.color.a * style.opacity, style.tint});
            }
        }
    }
    return result;
}
xrender::DisplayList at(const mo::Morph& morph, double t) {
    xrender::DisplayList list;
    morph.draw(t, list);
    return list;
}
void near(const mlibs::geom::Affine& a, const mlibs::geom::Affine& b, double tolerance = 0.002) {
    check_near(a.a, b.a, tolerance); check_near(a.b, b.b, tolerance);
    check_near(a.c, b.c, tolerance); check_near(a.d, b.d, tolerance);
    check_near(a.e, b.e, tolerance); check_near(a.f, b.f, tolerance);
}
void near(const mlibs::geom::Color& a, const mlibs::geom::Color& b, double tolerance = 0.002) {
    check_near(a.r, b.r, tolerance); check_near(a.g, b.g, tolerance);
    check_near(a.b, b.b, tolerance); check_near(a.a, b.a, tolerance);
}
}

int main(int argc, char** argv) {
    Suite suite{"motion.text-transition"};
    suite.test("kept glyph keeps source affine, opacity and tint at zero plus", [] {
        const auto g = glyphs({7});
        const xrender::GlyphStyle source{
            .transform = mlibs::geom::Affine::translate(40, 8) * mlibs::geom::Affine::rotation(0.8f, 0.6f) * mlibs::geom::Affine::scale(1.4f, 0.7f),
            .opacity = 0.35f, .tint = {0.9f, 0.15f, 0.1f, 0.8f}};
        const xrender::GlyphStyle target{.transform = mlibs::geom::Affine::rotation(0.6f, -0.8f), .opacity = 0.9f, .tint = {0.1f, 0.2f, 0.8f, 0.4f}};
        auto morph = transition(drawing(g, source, mlibs::geom::Affine::translate(7, 5), 17),
                                drawing(g, target, mlibs::geom::Affine::translate(90, 12) * mlibs::geom::Affine::scale(0.8f, 1.2f), 29));
        const auto start = visible(at(morph, 0));
        const auto next = visible(at(morph, 0.000001));
        check(start.size() == 1 && next.size() == 1);
        if (start.size() != 1 || next.size() != 1) return;
        near(next[0].outline, start[0].outline);
        check_near(next[0].opacity, start[0].opacity, 0.0001);
        near(next[0].tint, start[0].tint);
        const auto middle = visible(at(morph, 0.4));
        const auto end = visible(at(morph, 1));
        check(middle.size() == 1 && end.size() == 1);
        if (middle.size() == 1 && end.size() == 1) {
            check(middle[0].outline != start[0].outline && middle[0].outline != end[0].outline, "a real intermediate affine");
            check_near(middle[0].opacity, 0.35 + (0.9 - 0.35) * 0.4, 0.0001);
            near(end[0].outline, visible(drawing(g, target, mlibs::geom::Affine::translate(90, 12) * mlibs::geom::Affine::scale(0.8f, 1.2f), 29))[0].outline);
        }
    });
    suite.test("retarget starts from the styled snapshot, not its untransformed pens", [] {
        const auto g = glyphs({11, 12, 13});
        auto first = transition(drawing(g), drawing(g, {}, mlibs::geom::Affine::translate(90, 15) * mlibs::geom::Affine::rotation(0.8f, 0.6f), 26));
        const auto previous = visible(at(first, 0.37));
        auto target = drawing(g, {.transform = mlibs::geom::Affine::scale(0.9f, 1.1f), .opacity = 0.6f, .tint = {0.2f, 0.6f, 0.1f, 0.5f}}, {}, 18);
        mo::Morph second(first.snapshot(0.37), mo::Matter::extract(target, resolver()), linear(), 0);
        const auto next = visible(at(second, 0.000001));
        check_eq(next.size(), previous.size());
        for (std::size_t i = 0; i < std::min(next.size(), previous.size()); ++i) {
            near(next[i].outline, previous[i].outline);
            check_near(next[i].opacity, previous[i].opacity, 0.0001);
            near(next[i].tint, previous[i].tint);
        }
        const auto end = visible(at(second, 1));
        const auto expected = visible(target);
        check_eq(end.size(), expected.size());
        for (std::size_t i = 0; i < std::min(end.size(), expected.size()); ++i) near(end[i].outline, expected[i].outline);
    });
    suite.test("transparent tint retains straight RGB continuously in both directions and across retarget", [] {
        const auto g = glyphs({17});
        const mlibs::geom::Color clear{0.1f, 0.4f, 0.9f, 0};
        const mlibs::geom::Color tinted{0.8f, 0.2f, 0.1f, 0.7f};
        for (const bool reverse : {false, true}) {
            const auto source = reverse ? tinted : clear;
            const auto target = reverse ? clear : tinted;
            auto first = transition(drawing(g, {.tint = source}), drawing(g, {.tint = target}));
            const auto start = visible(at(first, 0));
            const auto next = visible(at(first, 0.000001));
            const auto before_end = visible(at(first, 0.999999));
            const auto end = visible(at(first, 1));
            check(start.size() == 1 && next.size() == 1 && before_end.size() == 1 && end.size() == 1);
            if (start.size() != 1 || next.size() != 1 || before_end.size() != 1 || end.size() != 1) continue;
            near(start[0].tint, source);
            near(next[0].tint, source);
            near(before_end[0].tint, target);
            near(end[0].tint, target);
            const auto middle = visible(at(first, 0.43));
            check(middle.size() == 1);
            if (middle.size() != 1) continue;
            check_near(middle[0].tint.a, source.a + (target.a - source.a) * 0.43, 0.0001);
            auto final = drawing(g, {.tint = {0.4f, 0.7f, 0.3f, 0.5f}});
            mo::Morph second(first.snapshot(0.43), mo::Matter::extract(final, resolver()), linear(), 0);
            const auto resumed = visible(at(second, 0.000001));
            check_eq(resumed.size(), middle.size());
            if (resumed.size() == 1) near(resumed[0].tint, middle[0].tint);
        }
    });
    suite.test("readable text keeps contiguous edits and replaces unrelated glyph coincidences", [] {
        auto options = linear(); options.text = mo::TextTransition::automatic;
        const auto save = glyphs({1, 2, 3, 4});
        const auto saved = glyphs({1, 2, 3, 4, 5});
        auto edit = transition(drawing(save), drawing(saved), options);
        check_eq(edit.stats().glyphs_kept, std::size_t{4});
        check_eq(edit.stats().text_runs_replaced, std::size_t{0});
        const auto a = glyphs({1, 2, 3, 4, 5, 6, 7, 8});
        const auto b = glyphs({9, 4, 8, 2, 10, 6, 11});
        auto replacement = transition(drawing(a), drawing(b), options);
        check_eq(replacement.stats().text_runs_replaced, std::size_t{1});
        check_eq(replacement.stats().glyphs_kept, std::size_t{0});
        for (const double t : {0.1, 0.25, 0.4, 0.6, 0.75, 0.9}) {
            const auto output = visible(at(replacement, t));
            const auto& expected = t < 0.5 ? a : b;
            check_eq(output.size(), expected.size());
            for (std::size_t i = 0; i < std::min(output.size(), expected.size()); ++i) {
                check_eq(output[i].id, expected[i].id);
                if (i) {
                    check_near(output[i].outline.e - output[0].outline.e, static_cast<float>(i) * 10, 0.0001);
                    check_near(output[i].outline.f, output[0].outline.f, 0.0001);
                }
                check(output[i].opacity > 0 && output[i].opacity < 1, "whole run really fades in flight");
            }
        }
        check(visible(at(replacement, 0.5)).empty(), "fade-through never overlays two unrelated sentences");
        const auto forward_hash = at(replacement, 0.25).hash();
        (void)at(replacement, 0.8);
        check_eq(at(replacement, 0.25).hash(), forward_hash);
        options.text = mo::TextTransition::glyphs;
        auto expressive = transition(drawing(a), drawing(b), options);
        check(expressive.stats().glyphs_kept > 0 && expressive.stats().text_runs_replaced == 0,
              "explicit glyph choreography remains available");
        options.text = mo::TextTransition::replace;
        auto explicit_replace = transition(drawing(save), drawing(saved), options);
        check_eq(explicit_replace.stats().text_runs_replaced, std::size_t{1});
        options.glyphs = false;
        auto legacy = transition(drawing(save), drawing(saved), options);
        check_eq(legacy.stats().text_runs_replaced, std::size_t{0});
        check_eq(visible(at(legacy, 0.25)).size(), save.size() + saved.size());
    });
    suite.test("automatic edits never match an incidental letter inside the replaced middle", [] {
        auto options = linear(); options.text = mo::TextTransition::automatic;
        const auto a = glyphs({1, 2, 3, 4, 5, 6, 7, 8, 9});
        const auto b = glyphs({1, 2, 3, 4, 7, 10, 5, 8, 9});
        auto edit = transition(drawing(a), drawing(b), options);
        // Four prefix and two suffix glyphs; coincident 7/5 in the middle
        // do not travel across the changed word just because LCS finds them.
        check_eq(edit.stats().glyphs_kept, std::size_t{6});
        check_eq(edit.stats().glyphs_new, std::size_t{3});
        check_eq(edit.stats().text_runs_replaced, std::size_t{0});
        auto reflowed = b;
        for (auto& glyph : reflowed) glyph.line = 1;
        auto reflow = transition(drawing(a), drawing(reflowed), options);
        check_eq(reflow.stats().text_runs_replaced, std::size_t{1});
        // A prefix cut inside a cluster cannot be used as a stable anchor.
        auto cluster_a = glyphs({1, 2, 3, 4});
        auto cluster_b = glyphs({1, 2, 3, 5});
        for (auto& glyph : cluster_a) glyph.cluster = 0;
        for (auto& glyph : cluster_b) glyph.cluster = 0;
        auto cluster = transition(drawing(cluster_a), drawing(cluster_b), options);
        check_eq(cluster.stats().text_runs_replaced, std::size_t{1});
    });
    suite.test("whole-run replacement retarget preserves the visible source and clip", [] {
        auto options = linear(); options.text = mo::TextTransition::replace;
        const auto a = glyphs({1, 2, 3});
        const auto b = glyphs({4, 5, 6, 7});
        const auto c = glyphs({8, 9});
        auto clipped = [&](const auto& glyphs) {
            xrender::DisplayList list;
            list.begin_node(1);
            list.push_transform(mlibs::geom::Affine::translate(9, 6));
            list.push_clip(mlibs::geom::Rect{12, 15, 95, 32});
            list.draw_glyphs(xrender::FontRef{1}, 20, {20, 40}, glyphs, xrender::Paint::solid(mlibs::geom::Color::hex(0x182030)));
            list.pop_clip(); list.pop_transform(); list.end_node();
            return list;
        };
        auto first = transition(clipped(a), clipped(b), options);
        for (const double t : {0.25, 0.75}) {
            const auto before = at(first, t);
            const auto source = visible(before);
            const auto matter = mo::Matter::extract(before, resolver());
            check(!matter.pieces().empty());
            for (const auto& piece : matter.pieces()) check(piece.clip >= 0, "original clip retained by replacement");
            auto target = clipped(c);
            mo::Morph next(first.snapshot(t), mo::Matter::extract(target, resolver()), options, 0);
            const auto after = visible(at(next, 0.000001));
            check_eq(after.size(), source.size());
            for (std::size_t i = 0; i < std::min(after.size(), source.size()); ++i) {
                check_eq(after[i].id, source[i].id);
                near(after[i].outline, source[i].outline);
                check_near(after[i].opacity, source[i].opacity, 0.0001);
            }
            const auto ending = visible(at(next, 1));
            const auto expected = visible(target);
            check_eq(ending.size(), expected.size());
            for (std::size_t i = 0; i < std::min(ending.size(), expected.size()); ++i) near(ending[i].outline, expected[i].outline);
        }
    });
    suite.test("a newly appearing glyph retains its fractional opacity when retargeted", [] {
        const auto a = glyphs({1, 2, 3});
        const auto b = glyphs({1, 2, 3, 4});
        auto first = transition(drawing(a), drawing(b));
        const auto previous = visible(at(first, 0.37));
        check(previous.size() == 4 && previous.back().opacity > 0 && previous.back().opacity < 0.5);
        auto target = drawing(b, {}, mlibs::geom::Affine::translate(40, 0));
        mo::Morph next(first.snapshot(0.37), mo::Matter::extract(target, resolver()), linear(), 0);
        const auto after = visible(at(next, 0.000001));
        check_eq(after.size(), previous.size());
        for (std::size_t i = 0; i < std::min(after.size(), previous.size()); ++i) {
            near(after[i].outline, previous[i].outline);
            check_near(after[i].opacity, previous[i].opacity, 0.0001);
        }
    });
    suite.test("zero-size and singular-transform boundaries stay finite and continuous", [] {
        const auto g = glyphs({1});
        auto morph = transition(drawing(g), drawing(g, {}, mlibs::geom::Affine::scale(0, 0), 0));
        const auto start = visible(at(morph, 0));
        const auto next = visible(at(morph, 0.000001));
        check(!start.empty() && !next.empty());
        if (!start.empty() && !next.empty()) {
            near(next.front().outline, start.front().outline);
            check_near(next.front().opacity, start.front().opacity, 0.0001);
        }
        for (const double t : {0.0, 0.25, 0.5, 0.75, 1.0})
            for (const auto& glyph : visible(at(morph, t))) {
                const auto& m = glyph.outline;
                check(std::isfinite(m.a) && std::isfinite(m.b) && std::isfinite(m.c) && std::isfinite(m.d) && std::isfinite(m.e) && std::isfinite(m.f));
            }
    });
    return suite.run(argc, argv);
}
