// openmui.view:glyphs — glyph motion: any node's text, animated glyph by
// glyph, grapheme, word or line (D-60 sub-object tier, D-68).
//
// A component paints its text as it always does; the runtime then restyles
// the glyphs that node and its descendants recorded
// (render::DisplayList::style_glyphs) — each descendant's text a paragraph
// of its own, a descendant with a motion of its own keeping it. So every
// text in every component — a label, a button, a whole Markdown document, a
// third party's widget — can be typed out, raised line by line, scattered
// and gathered, without the component doing anything.
//
// Each element (a glyph, grapheme, word or line inside the byte range) goes
// from `from` to `to` by the transition, delayed by its place in the order
// times `stagger`. Everything is a pure function of time since the motion
// started: it seeks, pauses and steps with the runtime's clock, and renders
// the same pixels for any frame rendered directly.
//
//     omui::animate_glyphs(text({.id = "title", .text = "Hello"}), omui::GlyphMotion::rise());
//     runtime.set(node, "glyph_motion", omui::to_value(omui::GlyphMotion::typewriter(seconds(0.04))));
export module openmui.view:glyphs;

import std;
import openmui.core;
import hanim;
import openmui.render.vector;
import :element;

export namespace omui {

enum class GlyphDomain : std::uint8_t { glyph, grapheme, word, line };
enum class GlyphOrder : std::uint8_t { forward, reverse, center, edges, random };

// The state of one element: offsets in logical pixels, a scale and a rotation
// (radians) about the glyph's origin on the baseline, an opacity, and a tint
// mixed over the text's colour by its alpha.
struct GlyphPose {
    float x = 0, y = 0;
    float scale = 1;
    float rotate = 0;
    float opacity = 1;
    Color tint{0, 0, 0, 0};
    friend bool operator==(const GlyphPose&, const GlyphPose&) = default;
};

struct GlyphMotion {
    GlyphDomain   domain = GlyphDomain::grapheme;
    std::size_t   begin = 0;                          // the bytes of the text it applies to
    std::size_t   end = std::numeric_limits<std::size_t>::max();
    GlyphPose     from{.opacity = 0};
    GlyphPose     to{};
    GlyphPose     jitter{.scale = 0, .opacity = 0};   // ± per element, seeded: where letters gather from or scatter to
    bool          jitter_to = false;                 // the jitter applies to `to` (scattering) rather than `from` (gathering)
    Transition    transition = Transition::tween(hanim::seconds(0.45), hanim::ease::out_cubic);
    hanim::seconds stagger{0.03};
    GlyphOrder    order = GlyphOrder::forward;
    std::uint64_t seed = 0;
    int           repeat = 1;          // cycles; 0: forever
    bool          alternate = false;   // every other cycle runs backwards (a wave)

    // Letters appearing one after another.
    [[nodiscard]] static GlyphMotion typewriter(hanim::seconds per_letter = hanim::seconds(0.035));
    // Words rising into place.
    [[nodiscard]] static GlyphMotion rise(hanim::seconds stagger = hanim::seconds(0.06));
    // Letters flying in from scattered places and turns.
    [[nodiscard]] static GlyphMotion gather(std::uint64_t seed = 1);
    // The reverse: letters flying apart and fading (to dismiss a text).
    [[nodiscard]] static GlyphMotion scatter(std::uint64_t seed = 1);
    // Letters bobbing in a travelling wave, forever.
    [[nodiscard]] static GlyphMotion wave(float height = 4);
    friend bool operator==(const GlyphMotion& a, const GlyphMotion& b) noexcept;
};

// The motion as a property value (every language, view documents, agents)
// and back; nullopt for a value that is not one.
[[nodiscard]] Value to_value(const GlyphMotion& m);
[[nodiscard]] std::optional<GlyphMotion> glyph_motion_from(const Value& v);

// An element with a glyph motion (it starts when the node mounts).
[[nodiscard]] Element animate_glyphs(Element e, const GlyphMotion& m);

// The styles of a node's glyphs at `elapsed` seconds into the motion: the
// glyphs of every command of the node, in recording order; `styles[i]` is
// glyph i's. `paragraphs` says which paragraph each command's glyphs are of
// (their graphemes, words and lines are counted per paragraph; empty: all one
// paragraph); elements go in paragraph order, then reading order. Answers
// when the motion ends (infinity when it repeats forever).
struct GlyphFrame {
    std::vector<std::vector<render::GlyphStyle>> styles;  // per command
    hanim::seconds                                ends{0};
    bool                                         moving = false;
};
[[nodiscard]] GlyphFrame glyph_frame(const GlyphMotion& m, hanim::seconds elapsed, std::span<const std::span<const render::Glyph>> commands,
                                     bool reduce_motion = false, std::span<const std::uint32_t> paragraphs = {});

}  // namespace omui
