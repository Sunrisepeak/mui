// hanim.core:shape — prepared, renderer-independent closed contour interpolation.
export module hanim.core:shape;

import std;
import mlibs.error;
import :value;
import :path;

export namespace hanim {

enum class ShapeVerb : std::uint8_t { line, quadratic, cubic };

// Only the controls used by verb are read. Closure is supplied by the contour.
struct ShapeSegment {
    ShapeVerb verb = ShapeVerb::line;
    Vec2 end{}, control1{}, control2{};
    [[nodiscard]] static constexpr ShapeSegment line(Vec2 end) noexcept { return {ShapeVerb::line, end}; }
    [[nodiscard]] static constexpr ShapeSegment quadratic(Vec2 control, Vec2 end) noexcept {
        return {ShapeVerb::quadratic, end, control};
    }
    [[nodiscard]] static constexpr ShapeSegment cubic(Vec2 a, Vec2 b, Vec2 end) noexcept {
        return {ShapeVerb::cubic, end, a, b};
    }
};

struct ShapeContour {
    Vec2 start{};
    std::span<const ShapeSegment> segments{};
    bool closed = true; // false is unsupported; true inserts a closing line if necessary
};

// One cubic Bezier, including its starting point. A sampled plan is a connected
// cyclic span of these; drawing it does not require access to either input.
struct ShapeCubic {
    Vec2 start{}, control1{}, control2{}, end{};
    [[nodiscard]] Vec2 point(double parameter) const noexcept;
};

enum class ShapeMorphError : std::uint8_t {
    empty_contour, open_contour, invalid_verb, nonfinite_coordinate,
    numerical_range, degenerate_contour, segment_budget, invalid_options,
    invalid_progress, output_too_small, unprepared, invalid_segment
};
// The codes as the error contract describes them (mlibs.error).
[[nodiscard]] constexpr mlibs::error::Description describe(ShapeMorphError e) noexcept {
    using Kind = mlibs::error::Kind;  // not a using-declaration: that is an ICE in GCC 16
    switch (e) {
        case ShapeMorphError::empty_contour: return {"hanim.shape.empty-contour", Kind::invalid_argument};
        case ShapeMorphError::open_contour: return {"hanim.shape.open-contour", Kind::invalid_argument};
        case ShapeMorphError::invalid_verb: return {"hanim.shape.invalid-verb", Kind::invalid_argument};
        case ShapeMorphError::nonfinite_coordinate: return {"hanim.shape.nonfinite-coordinate", Kind::invalid_argument};
        case ShapeMorphError::numerical_range: return {"hanim.shape.numerical-range", Kind::failed};
        case ShapeMorphError::degenerate_contour: return {"hanim.shape.degenerate-contour", Kind::invalid_argument};
        case ShapeMorphError::segment_budget: return {"hanim.shape.segment-budget", Kind::refused};
        case ShapeMorphError::invalid_options: return {"hanim.shape.invalid-options", Kind::invalid_argument};
        case ShapeMorphError::invalid_progress: return {"hanim.shape.invalid-progress", Kind::invalid_argument};
        case ShapeMorphError::output_too_small: return {"hanim.shape.output-too-small", Kind::invalid_argument};
        case ShapeMorphError::unprepared: return {"hanim.shape.unprepared", Kind::invalid_argument};
        case ShapeMorphError::invalid_segment: return {"hanim.shape.invalid-segment", Kind::invalid_argument};
    }
    return {"hanim.shape.?", Kind::internal};
}

struct ShapeMorphOptions {
    // Includes the implicit closing segment and subdivisions on both sides.
    // Hard limits keep preparation bounded even for untrusted input.
    std::size_t max_input_segments = 256; // 1..4096, per contour
    std::size_t max_output_segments = 1024; // 1..16384
    unsigned arc_samples = 64; // 4..256, chords per original cubic
    unsigned alignment_samples = 32; // 4..128, phase candidates and witnesses
    bool align_direction = true;
    bool align_start = true;
    PointPath point_path; // after correspondence, in each point's endpoint frame
};

// A closed contour's signed area (positive counter-clockwise in a y-up
// sense) and centroid, exact: mlibs.geom's measure of its cubics.
struct ShapeMeasure {
    double area = 0;
    Vec2   centroid{};
};
[[nodiscard]] std::expected<ShapeMeasure, ShapeMorphError> measure(ShapeContour contour, std::size_t max_segments = 4096);

// How two sets of contours correspond (convergence plan §6.8 ③ ⑤), the one
// rule shape morphs of objects and of drawings (glyphs) share: outlines pair
// with outlines and holes with holes (winding against the largest), pinned
// pairs first, then cheapest first — `distance` × centroid distance over the
// size plus `size` × |log of the area ratio| (ties by ordinal). A contour
// left over grows from a point or closes into one: an outline at the nearest
// outline of the other end, a hole at its own centre. A contour that is a
// single point is no contour. Pairs come first, then closing, then growing.
struct ContourPin {
    std::size_t from = 0, to = 0;
};
struct ContourRules {
    double                  distance = 1, size = 0.5;
    std::span<const ContourPin> pins;
};
struct ContourPair {
    static constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
    std::size_t from = none, to = none;
    Vec2        point{};  // where a contour alone grows from or closes into
};
enum class ContourPairError : std::uint8_t { open_contour, measure, invalid_pin };
struct ContourPairFailure {
    ContourPairError code = ContourPairError::open_contour;
    bool             from_end = true;  // which end's contour
    std::size_t      contour = 0;
    ShapeMorphError  cause = ShapeMorphError::invalid_options;  // a measure's
};
[[nodiscard]] std::expected<std::vector<ContourPair>, ContourPairFailure>
pair_contours(std::span<const ShapeContour> from, std::span<const ShapeContour> to, const ContourRules& rules = {});

// Single closed contour only. Callers supply simple nonzero-area contours;
// this first checkpoint does not certify simplicity, holes or topology changes.
// Preparation may allocate. It elevates degrees and SUBDIVIDES the original
// Beziers: the endpoint shapes are preserved, not replaced by sampled polygons.
// Arc length and cyclic phase selection are finite-resolution correspondences,
// not a guarantee of optimal pairing or simple intermediates for every input.
class PreparedShapeMorph {
public:
    [[nodiscard]] static std::expected<PreparedShapeMorph, ShapeMorphError>
        prepare(ShapeContour from, ShapeContour to, ShapeMorphOptions options = {});
    // A contour that appears from a point, or closes into one (topology
    // changes, convergence plan §6.8 ③): every point runs between `point` and
    // its place on the contour, so that end is the point itself. `reverse`
    // runs the grown contour backwards, keeping its winding beside contours
    // whose targets were reversed (target_reversed()).
    [[nodiscard]] static std::expected<PreparedShapeMorph, ShapeMorphError>
        grow(Vec2 point, ShapeContour to, ShapeMorphOptions options = {}, bool reverse = false);
    [[nodiscard]] static std::expected<PreparedShapeMorph, ShapeMorphError>
        shrink(ShapeContour from, Vec2 point, ShapeMorphOptions options = {});

    [[nodiscard]] std::size_t segment_count() const noexcept { return from_.size(); }
    [[nodiscard]] bool target_reversed() const noexcept { return reversed_; }
    // Fraction of the oriented target perimeter used as its new start.
    [[nodiscard]] double target_start_fraction() const noexcept { return phase_; }
    [[nodiscard]] PointPath point_path() const noexcept { return point_path_; }

    // Arbitrary order / concurrent reads with distinct buffers. Finite progress
    // clamps to [0,1]; NaN/Inf and insufficient buffers fail WITHOUT writing.
    // No allocation, matching, callbacks or renderer dependency here.
    [[nodiscard]] std::expected<std::size_t, ShapeMorphError>
        sample(double progress, std::span<ShapeCubic> output) const noexcept;
    // Streaming consumers can write directly into their own arena without a
    // temporary vector. Same progress policy as sample; no mutable scratch.
    [[nodiscard]] std::expected<ShapeCubic, ShapeMorphError>
        sample_segment(double progress, std::size_t segment) const noexcept;

private:
    std::vector<ShapeCubic> from_, to_;
    bool reversed_ = false;
    double phase_ = 0;
    PointPath point_path_;
};

} // namespace hanim
