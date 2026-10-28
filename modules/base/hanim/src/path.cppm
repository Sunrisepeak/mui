// hanim:path — motion along a curve, parameterised by distance.
//
// Animating "progress along a path" from 0 to 1 should move at the speed the
// ease says, not faster on long control-point spans; so the path is sampled
// into an arc-length table once, and a position is looked up by length.
export module hanim:path;

import std;
import :math;
import :value;

export namespace hanim {

class MotionPath {
public:
    MotionPath& move_to(Vec2 p);
    MotionPath& line_to(Vec2 p);
    MotionPath& quad_to(Vec2 control, Vec2 p);
    MotionPath& cubic_to(Vec2 c1, Vec2 c2, Vec2 p);
    MotionPath& close();

    [[nodiscard]] double length() const noexcept { return total_; }
    [[nodiscard]] bool empty() const noexcept { return segments_.empty(); }

    // Where the path is after the fraction u ∈ [0, 1] of its length, and the
    // direction of travel there (radians, atan2 of the tangent).
    struct Pose {
        Vec2   position;
        double angle = 0;
    };
    [[nodiscard]] Pose at(double u) const noexcept;
    [[nodiscard]] Vec2 point(double u) const noexcept { return at(u).position; }

private:
    static constexpr int samples = 32;  // per segment; enough for UI-scale curves
    struct Segment {
        Vec2 p0, c1, c2, p1;            // a line or quadratic is stored as a cubic
        std::array<double, samples + 1> cumulative{};  // arc length at parameter i/samples
        double start = 0;               // arc length before this segment
    };
    void append(Vec2 c1, Vec2 c2, Vec2 p1);

    std::vector<Segment> segments_;
    Vec2   cursor_{};
    Vec2   subpath_start_{};
    double total_ = 0;
};

}  // namespace hanim
