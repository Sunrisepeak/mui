// hanim.core:path — motion along a curve, parameterised by distance.
//
// Animating "progress along a path" from 0 to 1 should move at the speed the
// ease says, not faster on long control-point spans; so the path is sampled
// into an arc-length table once, and a position is looked up by length.
export module hanim.core:path;

import std;
import :math;
import :value;

export namespace hanim {

// A displacement along the way by a coherent noise field (convergence plan
// §6.8 ⑤), in the points' own units: each point samples the field where it
// starts (so a contour's shared vertices move alike), the field drifts
// `speed` features over the course, and an envelope 16s²(1−s)² holds both
// ends and their speeds exactly. Pure in (point, progress, seed): any seek
// order gives the same frame. mlibs.math's gradient or curl noise.
struct PathNoise {
    enum class Field : std::uint8_t { gradient, curl };
    Field         field = Field::gradient;
    double        amplitude = 0;  // the largest displacement, halfway
    double        scale = 1;      // the field's feature size
    double        speed = 1;      // features drifted over the whole course
    std::uint64_t seed = 0;
    friend constexpr bool operator==(const PathNoise&, const PathNoise&) noexcept = default;
};

// A point-to-point interpolation modifier, not a route in world space.
// Each control is an offset from the straight cubic's control in the
// endpoint chord frame: x along (to-from), y along its left normal (-dy,dx).
// Thus the same data works on vertices, translations or any other Vec2
// channel, independent of position, orientation and scale. Zero is linear.
struct PointPath {
    Vec2 control1{}, control2{};
    PathNoise noise{};  // added to the curve; absolute, not in the chord frame
    friend constexpr bool operator==(const PointPath& a, const PointPath& b) noexcept {
        return a.control1 == b.control1 && a.control2 == b.control2 && a.noise == b.noise;
    }
    // Bounded dimensionless controls; consumers validate once at prepare.
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] constexpr bool linear() const noexcept {
        return control1 == Vec2{} && control2 == Vec2{} && noise.amplitude == 0;
    }
    // Jitter (gradient noise) and swirl (curl noise) along the way.
    [[nodiscard]] static constexpr PointPath jitter(double amplitude, double scale, std::uint64_t seed = 0, double speed = 1) noexcept {
        return {{}, {}, {PathNoise::Field::gradient, amplitude, scale, speed, seed}};
    }
    [[nodiscard]] static constexpr PointPath swirl(double amplitude, double scale, std::uint64_t seed = 0, double speed = 1) noexcept {
        return {{}, {}, {PathNoise::Field::curl, amplitude, scale, speed, seed}};
    }
    // Midpoint displacement as a signed fraction of chord length. Positive
    // follows (-dy,dx); with screen coordinates positive y points down.
    [[nodiscard]] static constexpr PointPath bend(double amount) noexcept {
        return {{0, amount * (4.0 / 3.0)}, {0, amount * (4.0 / 3.0)}};
    }
    // Additive displacement in the SAME endpoint frame. This is not ordered
    // function composition; render effects have their separate ordered chain.
    // A path has one noise: the other's, when it has one.
    [[nodiscard]] constexpr PointPath added(PointPath other) const noexcept {
        return {{control1.x + other.control1.x, control1.y + other.control1.y},
                {control2.x + other.control2.x, control2.y + other.control2.y},
                other.noise.amplitude != 0 ? other.noise : noise};
    }
    // Pure, allocation-free point evaluation. Requires valid controls and
    // finite inputs. Like lerp, the baseline extrapolates outside [0,1]; the
    // modifier is zero outside that interval, including the exact endpoints.
    // A checked consumer also validates the resulting numerical range.
    [[nodiscard]] Vec2 interpolate(Vec2 from, Vec2 to, double progress) const noexcept;
};

class MotionPath {
public:
    MotionPath& move_to(Vec2 p);
    MotionPath& line_to(Vec2 p);
    MotionPath& quad_to(Vec2 control, Vec2 p);
    MotionPath& cubic_to(Vec2 c1, Vec2 c2, Vec2 p);
    MotionPath& close();

    [[nodiscard]] double length() const noexcept { return total_; }
    [[nodiscard]] bool empty() const noexcept { return segments_.empty(); }
    [[nodiscard]] std::size_t segment_count() const noexcept { return segments_.size(); }
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool continuous() const noexcept;
    static constexpr int arc_samples_per_segment = 32;

    // Where the path is after the fraction u ∈ [0, 1] of its length, and the
    // direction of travel there (radians, atan2 of the tangent).
    struct Pose {
        Vec2   position;
        double angle = 0;
    };
    [[nodiscard]] Pose at(double u) const noexcept;
    [[nodiscard]] Vec2 point(double u) const noexcept;

private:
    static constexpr int samples = arc_samples_per_segment;  // per segment; enough for UI-scale curves
    struct Segment {
        Vec2 p0, c1, c2, p1;            // a line or quadratic is stored as a cubic
        std::array<double, samples + 1> cumulative{};  // arc length at parameter i/samples
        double start = 0;               // arc length before this segment
    };
    void append(Vec2 c1, Vec2 c2, Vec2 p1);
    [[nodiscard]] std::pair<std::size_t,double> locate(double u) const noexcept;

    std::vector<Segment> segments_;
    Vec2   cursor_{};
    Vec2   subpath_start_{};
    double total_ = 0;
};

}  // namespace hanim
