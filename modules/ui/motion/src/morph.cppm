// openmui.motion:morph — a drawing moving to another (universal motion §6).
//
// Two matters of the same node — what it drew before a change and what it
// draws after — are matched piece to piece: by node and place (the same key,
// class and ordinal), then by meaning (a node whose key changed but whose
// role and name did not), then, inside two matched text runs, glyph by glyph
// (their longest common subsequence: "Save" → "Saved ✓" keeps S, a, v, e).
// A morph draws between them at any time as a pure function of it: matched
// pieces travel (their transform decomposed and eased, their rectangles,
// radii, colours in OKLab, prepared single-contour Bezier paths, glyph positions and
// sizes), what only the first has fades away, what only the second has
// fades in. Before its start it draws the first drawing; once settled, the
// second exactly. Nothing in it is a component's: a toggle's thumb, a list's
// order, a label's text and a button's colour move by the same rules.
export module openmui.motion:morph;

import std;
import hanim.core;
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

// Explicit glyph choreography remains the compatible default. Automatic
// keeps a substantial contiguous edit (Save -> Saved), otherwise replaces
// the whole run. Replacement fades through, with a small vertical movement;
// it preserves the drawing's clips instead of inventing font ink bounds.
enum class TextTransition : std::uint8_t { glyphs, replace, automatic };

// Nullopt means unknown; an empty rectangle means a known blank drawing.
// A callback used by concurrent draws must serialize any mutable font cache.
using GlyphBounds = std::function<std::optional<Rect>(const render::DrawGlyphs&, const Affine&)>;
// Exact masks for simple placement; transformed actual outline control-point
// bounds (and the renderer's colour-bitmap fallback) otherwise. Unknown
// coverage-only transformed bitmaps never become an em approximation.
[[nodiscard]] std::optional<Rect> glyph_ink_bounds(const render::DrawGlyphs& run, const Affine& transform, render::GlyphSource& source);

struct Options {
    Curve  curve;
    Order  order = Order::together;
    double stagger = 0;      // seconds between one piece's start and the next's, in `order`
    bool   semantic = true;  // match nodes whose key changed by their meaning
    bool   glyphs = true;    // false retains the legacy whole-run crossfade
    TextTransition text = TextTransition::glyphs;
    float  text_rise = 0.35f; // whole-run replacement travel, in ems (0: fade only)
    // Guard unchanged opaque solid text on a provable solid rect/rrect.
    // Translucent carriers require safe endpoints and a known opaque uniform
    // composition under the actual emitted glyphs. Zero preserves compatibility.
    // Domain: 0 or [1, 4.5]. Direct C++ values are canonicalized into this
    // range (nonfinite/negative -> off); options() exposes the actual value.
    // Higher thresholds cannot be guaranteed by ink alone on arbitrary
    // intermediate backgrounds; those need a background policy as well.
    double minimum_text_contrast = 0;
    GlyphBounds glyph_bounds; // actual-font proof for nested drawings and translucent carriers
    float proof_scale = 1;    // renderer's initial logical-to-device scale for clip/effect/AA proof
    std::function<float()> current_proof_scale; // optional live scale, read once per draw; invalid -> no deferred proof
    double fade_out = 0.6;   // the progress by which what only the first drawing has is gone
    double fade_in = 0.3;    // the progress at which what only the second has begins to show
    // What only one drawing has appears from (and vanishes to), about the
    // middle of all it draws: lower by `rise`, scaled by `grow` — a dialog
    // opening, a menu dropping, a node entering.
    float  rise = 0;
    float  grow = 1;
    bool   rebase_layout = false; // external allocation, internal state still morphs
    std::vector<std::string> layout_scopes; // stable owner keys; empty means entire matter
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
    std::size_t text_runs_replaced = 0;  // whole-run transitions, never scattered glyphs
    std::size_t text_contrast_runs = 0; // endpoint-qualified logical runs; current coverage is rechecked
    std::size_t by_meaning = 0;  // pairs matched by their nodes' meaning
    std::size_t paths_prepared = 0, paths_compatible = 0, paths_unsupported = 0;
};

enum class PathMorphMode : std::uint8_t { compatible, prepared, unsupported };
enum class PathMorphIssue : std::uint8_t { none, unsupported_contours, malformed_path, geometry };
enum class PathMaterialMode : std::uint8_t { interpolated, prepared_mix, unsupported };
struct PathMorphStatus {
    std::size_t from_piece = 0, to_piece = 0;
    PathMorphMode mode = PathMorphMode::unsupported;
    PathMorphIssue issue = PathMorphIssue::none;
    std::optional<hanim::ShapeMorphError> geometry_error;
    PathMaterialMode material_mode = PathMaterialMode::interpolated;
    std::optional<render::PaintMixError> material_error;
};

// Draws a nested root's range where the morph places it (its marker; into `out`).
using NestedDraw = std::function<void(std::uint32_t marker, render::DisplayList& out)>;

// Actual drawing before this root at the sampled time. It may contain open
// ancestor transforms/clips/nodes; it is borrowed only for the proof during
// snapshot(). None of its commands or owners enter the returned Matter.
struct SnapshotContext {
    std::span<const render::Command> prefix;
};

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
    // Supplies underpaint needed by a translucent carrier. The root is still
    // evaluated at t, not replayed from a last-frame cache. Without context,
    // no external backdrop is inferred (the compatible isolated-root API).
    [[nodiscard]] Matter snapshot(double t, const NestedDraw& nested, const SnapshotContext& context) const;

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
    // Preparation outcome for matched paths. Unsupported topology/geometry
    // remains observable instead of being reported as a continuous morph.
    [[nodiscard]] std::span<const PathMorphStatus> path_morphs() const noexcept { return path_status_; }
    // How strongly its travel effect is on at `t`: 0 at rest, 1 half-way.
    [[nodiscard]] double travel(double t) const noexcept;

private:
    struct Pair {
        int    a = -1, b = -1;  // pieces of from and to; -1: none
        double delay = 0;
        std::vector<int>  glyph_from;  // per glyph of b: the glyph of a it comes from (-1: new)
        std::vector<char> glyph_kept;  // per glyph of a: whether some glyph of b comes from it
        std::optional<hanim::PreparedShapeMorph> path;
        std::optional<render::PreparedPaintMix> paint;
        bool path_compatible = false; // direct verb/point interpolation, including legacy open paths
        bool replace_text = false;
        int text_carrier = -1; // cached pair index; never a per-frame background search
        bool text_static_bounds = false; // safe even during spring overshoot
        bool text_deferred_groups = false; // check this draw's expanded nested ranges before changing ink
        bool text_composite = false; // resolve actual uniform opaque underpaint from the output prefix
        bool text_exact_geometry = false; // actual current glyph witness replaces rough interpolated run bounds
    };
    struct FramePair {
        int a = -1, b = -1;
        double delay = 0; // borrowed from an existing visible owner carrier
        bool stretch = false;
    };
    std::vector<FramePair> frames_; // not visible pieces or stagger ranks
    Matter            from_, to_;
    Options           options_;
    double            start_ = 0, end_ = 0;
    std::vector<Pair> pairs_;   // in drawing order
    MorphStats        stats_;
    std::vector<PathMorphStatus> path_status_;
    bool              deferred_contrast_ = false;
    void draw(double t, render::DisplayList& out, const NestedDraw& nested, bool snapshot, const render::PushLayer* live) const;
};

// A transform between two, decomposed (translation, rotation the short way,
// scale, shear) and eased by `u` — what a moving piece is drawn under.
[[nodiscard]] Affine mix(const Affine& a, const Affine& b, double u) noexcept;

}  // namespace omui::motion
