// hanim.core:shape — prepared, renderer-independent closed contour interpolation.
export module hanim.core:shape;

import std;
import :value;

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
[[nodiscard]] std::string_view shape_morph_error_name(ShapeMorphError error) noexcept;

struct ShapeMorphOptions {
    // Includes the implicit closing segment and subdivisions on both sides.
    // Hard limits keep preparation bounded even for untrusted input.
    std::size_t max_input_segments = 256; // 1..4096, per contour
    std::size_t max_output_segments = 1024; // 1..16384
    unsigned arc_samples = 64; // 4..256, chords per original cubic
    unsigned alignment_samples = 32; // 4..128, phase candidates and witnesses
    bool align_direction = true;
    bool align_start = true;
};

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

    [[nodiscard]] std::size_t segment_count() const noexcept { return from_.size(); }
    [[nodiscard]] bool target_reversed() const noexcept { return reversed_; }
    // Fraction of the oriented target perimeter used as its new start.
    [[nodiscard]] double target_start_fraction() const noexcept { return phase_; }

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
};

} // namespace hanim
