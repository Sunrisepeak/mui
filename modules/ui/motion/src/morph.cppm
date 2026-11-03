// openmui.motion:morph — a drawing moving to another (universal motion §6).
//
// Two matters of the same node — what it drew before a change and what it
// draws after — are matched piece to piece: by node and place (the same key,
// class and ordinal), then by meaning (a node whose key changed but whose
// role and name did not), then, inside two matched text runs, glyph by glyph
// (their longest common subsequence: "Save" → "Saved ✓" keeps S, a, v, e).
// A morph draws between them at any time as a pure function of it: matched
// pieces travel (their transform decomposed and eased, their rectangles,
// radii, colours in OKLab, paths of the same verbs, glyph positions and
// sizes), what only the first has fades away, what only the second has
// fades in. Before its start it draws the first drawing; once settled, the
// second exactly. Nothing in it is a component's: a toggle's thumb, a list's
// order, a label's text and a button's colour move by the same rules.
export module openmui.motion:morph;

import std;
import hanim;
import openmui.geom;
import openmui.render.vector;
import :matter;

export namespace omui::motion {

// How a morph's progress goes from 0 to 1 over time (seconds since its start).
struct Curve {
    enum class Kind : std::uint8_t { tween, spring };
    Kind                kind = Kind::spring;
    double              duration = 0.3;  // a tween's
    hanim::Ease          ease = hanim::ease::standard;
    hanim::SpringParams  spring = hanim::springs::standard;
    double              velocity = 0;  // progress a second at its start (an interrupted morph's, carried on)

    [[nodiscard]] double at(double t) const noexcept;
    [[nodiscard]] double rate(double t) const noexcept;
    [[nodiscard]] double length() const noexcept;  // until it rests at 1
};

// In which order pieces start when they are staggered.
enum class Order : std::uint8_t { together, document, distance };

struct Options {
    Curve  curve;
    Order  order = Order::together;
    double stagger = 0;      // seconds between one piece's start and the next's, in `order`
    bool   semantic = true;  // match nodes whose key changed by their meaning
    bool   glyphs = true;    // match glyphs inside matched text runs
    double fade_out = 0.6;   // the progress by which what only the first drawing has is gone
    double fade_in = 0.3;    // the progress at which what only the second has begins to show
    // What only one drawing has appears from (and vanishes to), about the
    // middle of all it draws: lower by `rise`, scaled by `grow` — a dialog
    // opening, a menu dropping, a node entering.
    float  rise = 0;
    float  grow = 1;
    bool   local_presence = false;  // scale an appearing part about its own bounds
    // A moving fill (or effect shape) drawn out along its way: its leading edge
    // ahead by `stretch` of the distance left (at most 40% of its width), a
    // little thinner — a drop pulled, not a box moved; it rests round.
    float  stretch = 0;
    // While it moves, all it draws goes through an effect (a liquid's drops
    // merging, ink's line wavering): the effect's `param` from its value in
    // `params` to `peak` half-way and back, so it is not there at either end
    // — an effect a motion carries (component × effect × motion, D-93). The
    // layer's shapes are the moving fills where they are.
    struct Travel {
        render::EffectRef  effect;
        std::vector<float> params;
        int                param = -1;
        float              peak = 0;
        friend bool operator==(const Travel&, const Travel&) = default;
    };
    std::optional<Travel> travel;
};

struct MorphStats {
    std::size_t matched = 0, from_only = 0, to_only = 0;
    std::size_t glyphs_kept = 0, glyphs_new = 0, glyphs_gone = 0;
    std::size_t by_meaning = 0;  // pairs matched by their nodes' meaning
};

// Draws a nested root's range where the morph places it (its marker; into `out`).
using NestedDraw = std::function<void(std::uint32_t marker, render::DisplayList& out)>;

class Morph {
public:
    Morph(Matter from, Matter to, Options options, double start);
    Morph(Morph&&) noexcept = default;
    Morph& operator=(Morph&&) noexcept = default;

    // The drawing at `t` (seconds, the same clock as `start`), appended to `out`;
    // each piece inside its node's marker (the second drawing's markers).
    // `live`: the wrapper as the node draws it now — its effect's numbers taken
    // as they are (a style's state transition goes on while the pieces move).
    void draw(double t, render::DisplayList& out, const NestedDraw& nested = {}, const render::PushLayer* live = nullptr) const;
    // The drawing at `t` as matter again, its pieces keeping their nodes' keys:
    // where a morph interrupted by another change starts from.
    [[nodiscard]] Matter snapshot(double t, const NestedDraw& nested = {}) const;

    [[nodiscard]] double start() const noexcept { return start_; }
    // When every piece rests.
    [[nodiscard]] double end() const noexcept { return end_; }
    [[nodiscard]] bool settled(double t) const noexcept { return t >= end_; }
    // The curve's progress at `t` (no piece's delay), and its rate.
    [[nodiscard]] double progress(double t) const noexcept { return options_.curve.at(std::max(0.0, t - start_)); }
    [[nodiscard]] double rate(double t) const noexcept { return t < start_ ? 0 : options_.curve.rate(t - start_); }
    [[nodiscard]] const Matter& from() const noexcept { return from_; }
    [[nodiscard]] const Matter& to() const noexcept { return to_; }
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    [[nodiscard]] const MorphStats& stats() const noexcept { return stats_; }
    // How strongly its travel effect is on at `t`: 0 at rest, 1 half-way.
    [[nodiscard]] double travel(double t) const noexcept;

private:
    struct Pair {
        int    a = -1, b = -1;  // pieces of from and to; -1: none
        double delay = 0;
        std::vector<int>  glyph_from;  // per glyph of b: the glyph of a it comes from (-1: new)
        std::vector<char> glyph_kept;  // per glyph of a: whether some glyph of b comes from it
    };
    Matter            from_, to_;
    Options           options_;
    double            start_ = 0, end_ = 0;
    std::vector<Pair> pairs_;   // in drawing order
    MorphStats        stats_;
    void draw(double t, render::DisplayList& out, const NestedDraw& nested, bool snapshot, const render::PushLayer* live) const;
};

// A transform between two, decomposed (translation, rotation the short way,
// scale, shear) and eased by `u` — what a moving piece is drawn under.
[[nodiscard]] Affine mix(const Affine& a, const Affine& b, double u) noexcept;

}  // namespace omui::motion
