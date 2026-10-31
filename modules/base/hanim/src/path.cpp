module hanim;

import std;

namespace hanim {

namespace {

Vec2 cubic_point(const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, double t) noexcept {
    double u = 1.0 - t;
    double a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
    return {a * p0.x + b * c1.x + c * c2.x + d * p1.x, a * p0.y + b * c1.y + c * c2.y + d * p1.y};
}

Vec2 cubic_tangent(const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, double t) noexcept {
    double u = 1.0 - t;
    double a = 3.0 * u * u, b = 6.0 * u * t, c = 3.0 * t * t;
    return {a * (c1.x - p0.x) + b * (c2.x - c1.x) + c * (p1.x - c2.x),
            a * (c1.y - p0.y) + b * (c2.y - c1.y) + c * (p1.y - c2.y)};
}

double distance(const Vec2& a, const Vec2& b) noexcept {
    double dx = b.x - a.x, dy = b.y - a.y;
    return dm::sqrt(dx * dx + dy * dy);
}

}  // namespace

MotionPath& MotionPath::move_to(Vec2 p) {
    cursor_ = p;
    subpath_start_ = p;
    return *this;
}

MotionPath& MotionPath::line_to(Vec2 p) {
    // A line as a cubic with controls at its thirds, so it is uniform in t.
    Vec2 c1{cursor_.x + (p.x - cursor_.x) / 3.0, cursor_.y + (p.y - cursor_.y) / 3.0};
    Vec2 c2{cursor_.x + 2.0 * (p.x - cursor_.x) / 3.0, cursor_.y + 2.0 * (p.y - cursor_.y) / 3.0};
    append(c1, c2, p);
    return *this;
}

MotionPath& MotionPath::quad_to(Vec2 q, Vec2 p) {
    Vec2 c1{cursor_.x + 2.0 / 3.0 * (q.x - cursor_.x), cursor_.y + 2.0 / 3.0 * (q.y - cursor_.y)};
    Vec2 c2{p.x + 2.0 / 3.0 * (q.x - p.x), p.y + 2.0 / 3.0 * (q.y - p.y)};
    append(c1, c2, p);
    return *this;
}

MotionPath& MotionPath::cubic_to(Vec2 c1, Vec2 c2, Vec2 p) {
    append(c1, c2, p);
    return *this;
}

MotionPath& MotionPath::close() {
    if (cursor_ != subpath_start_) line_to(subpath_start_);
    return *this;
}

void MotionPath::append(Vec2 c1, Vec2 c2, Vec2 p1) {
    Segment s;
    s.p0 = cursor_;
    s.c1 = c1;
    s.c2 = c2;
    s.p1 = p1;
    s.start = total_;
    Vec2 prev = s.p0;
    double acc = 0;
    s.cumulative[0] = 0;
    for (int i = 1; i <= samples; ++i) {
        Vec2 q = cubic_point(s.p0, s.c1, s.c2, s.p1, static_cast<double>(i) / samples);
        acc += distance(prev, q);
        s.cumulative[i] = acc;
        prev = q;
    }
    total_ += acc;
    segments_.push_back(s);
    cursor_ = p1;
}

MotionPath::Pose MotionPath::at(double u) const noexcept {
    if (segments_.empty()) return {cursor_, 0};
    double target = dm::clamp(u, 0.0, 1.0) * total_;
    auto it = std::ranges::upper_bound(segments_, target, {}, &Segment::start);
    const Segment& s = it == segments_.begin() ? segments_.front() : *(it - 1);
    double local = target - s.start;
    auto c = std::ranges::upper_bound(s.cumulative, local);
    int i = static_cast<int>(c - s.cumulative.begin());
    if (i < 1) i = 1;
    if (i > samples) i = samples;
    double l0 = s.cumulative[i - 1], l1 = s.cumulative[i];
    double frac = l1 > l0 ? (local - l0) / (l1 - l0) : 0.0;
    double t = (static_cast<double>(i - 1) + dm::clamp(frac, 0.0, 1.0)) / samples;
    Vec2 tan = cubic_tangent(s.p0, s.c1, s.c2, s.p1, t);
    return {cubic_point(s.p0, s.c1, s.c2, s.p1, t), dm::atan2(tan.y, tan.x)};
}

}  // namespace hanim
