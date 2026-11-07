module hanim.core;

import std;

namespace hanim {
namespace {

Vec2 shape_mix(Vec2 a, Vec2 b, double t) noexcept {
    if (t == 0) return a;
    if (t == 1) return b;
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}
std::pair<ShapeCubic, ShapeCubic> split_shape(const ShapeCubic& c, double t) noexcept {
    const auto a = shape_mix(c.start, c.control1, t), b = shape_mix(c.control1, c.control2, t);
    const auto d = shape_mix(c.control2, c.end, t);
    const auto e = shape_mix(a, b, t), f = shape_mix(b, d, t), p = shape_mix(e, f, t);
    return {{c.start, a, e, p}, {p, f, d, c.end}};
}
ShapeCubic portion(const ShapeCubic& c, double begin, double end) noexcept {
    if (begin == 0 && end == 1) return c;
    auto left = end == 1 ? c : split_shape(c, end).first;
    return begin == 0 ? left : split_shape(left, begin / end).second;
}
bool finite(Vec2 p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }
bool in_range(Vec2 p) noexcept { return std::abs(p.x) <= 1e100 && std::abs(p.y) <= 1e100; }
std::expected<std::vector<ShapeCubic>, ShapeMorphError> cubics(ShapeContour contour, std::size_t budget) {
    if (!contour.closed) return std::unexpected(ShapeMorphError::open_contour);
    if (contour.segments.empty()) return std::unexpected(ShapeMorphError::empty_contour);
    if (contour.segments.size() > budget) return std::unexpected(ShapeMorphError::segment_budget);
    auto valid = [](Vec2 p) -> std::expected<void, ShapeMorphError> {
        if (!finite(p)) return std::unexpected(ShapeMorphError::nonfinite_coordinate);
        if (!in_range(p)) return std::unexpected(ShapeMorphError::numerical_range);
        return {};
    };
    if (auto ok = valid(contour.start); !ok) return std::unexpected(ok.error());
    std::vector<ShapeCubic> result;
    result.reserve(std::min(budget, contour.segments.size() + 1));
    Vec2 cursor = contour.start;
    auto append_line = [&](Vec2 end) {
        result.push_back({cursor, shape_mix(cursor, end, 1.0 / 3), shape_mix(cursor, end, 2.0 / 3), end});
        cursor = end;
    };
    for (const auto& segment : contour.segments) {
        if (auto ok = valid(segment.end); !ok) return std::unexpected(ok.error());
        if (segment.verb == ShapeVerb::line) append_line(segment.end);
        else if (segment.verb == ShapeVerb::quadratic || segment.verb == ShapeVerb::cubic) {
            if (auto ok = valid(segment.control1); !ok) return std::unexpected(ok.error());
            if (segment.verb == ShapeVerb::quadratic) {
                result.push_back({cursor, shape_mix(cursor, segment.control1, 2.0 / 3),
                                  shape_mix(segment.end, segment.control1, 2.0 / 3), segment.end});
            } else {
                if (auto ok = valid(segment.control2); !ok) return std::unexpected(ok.error());
                result.push_back({cursor, segment.control1, segment.control2, segment.end});
            }
            cursor = segment.end;
        } else return std::unexpected(ShapeMorphError::invalid_verb);
    }
    if (cursor != contour.start) {
        if (result.size() == budget) return std::unexpected(ShapeMorphError::segment_budget);
        append_line(contour.start);
    }
    return result;
}

// Exact polynomial integral of (x*y' - y*x') / 2. The local origin reduces
// cancellation for translated contours. Coordinates are additionally scaled by
// their control bounds before integration, preventing tiny-area underflow.
// This returns area / extent^2; no flattening decides orientation.
double area(std::span<const ShapeCubic> curves) noexcept {
    const auto origin = curves.front().start;
    Vec2 lo = origin, hi = origin;
    for (const auto& c : curves) for (auto p : {c.start, c.control1, c.control2, c.end}) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    const double scale = std::max(hi.x - lo.x, hi.y - lo.y);
    if (scale == 0) return 0;
    double result = 0;
    for (const auto& c : curves) {
        auto coefficients = [scale](double p, double a, double b, double q, double o) {
            p = (p - o) / scale; a = (a - o) / scale;
            b = (b - o) / scale; q = (q - o) / scale;
            return std::array<double, 4>{p, 3 * (a - p), 3 * (p - 2 * a + b), q - p + 3 * (a - b)};
        };
        const auto x = coefficients(c.start.x, c.control1.x, c.control2.x, c.end.x, origin.x);
        const auto y = coefficients(c.start.y, c.control1.y, c.control2.y, c.end.y, origin.y);
        for (unsigned i = 0; i < 4; ++i)
            for (unsigned j = 1; j < 4; ++j)
                result += (x[i] * static_cast<double>(j) * y[j] - y[i] * static_cast<double>(j) * x[j]) /
                          static_cast<double>(i + j);
    }
    return result * 0.5;
}
void reverse(std::vector<ShapeCubic>& curves) {
    std::ranges::reverse(curves);
    for (auto& c : curves) { std::swap(c.start, c.end); std::swap(c.control1, c.control2); }
}
struct Metric {
    double start = 0, end = 0, length = 0;
    std::array<double, 257> lengths{};
};
struct Measured {
    std::span<const ShapeCubic> curves;
    std::vector<Metric> metrics;
    unsigned samples;
    double length = 0;
    Vec2 center{};
    double scale = 0;

    Measured(std::span<const ShapeCubic> input, unsigned count) : curves(input), samples(count) {
        metrics.reserve(input.size());
        Vec2 lo = input.front().start, hi = lo;
        for (const auto& c : input) {
            for (auto p : {c.start, c.control1, c.control2, c.end}) {
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
            }
            Metric m;
            m.start = length;
            Vec2 previous = c.start;
            for (unsigned i = 1; i <= samples; ++i) {
                const auto point = c.point(static_cast<double>(i) / samples);
                const double dx = point.x - previous.x, dy = point.y - previous.y;
                const double large = std::max(std::abs(dx), std::abs(dy));
                const double small = std::min(std::abs(dx), std::abs(dy));
                const double ratio = large > 0 ? small / large : 0;
                m.length += large * dm::sqrt(1 + ratio * ratio);
                m.lengths[i] = m.length;
                previous = point;
            }
            length += m.length;
            m.end = length;
            metrics.push_back(m);
        }
        center = shape_mix(lo, hi, 0.5);
        scale = std::max(hi.x - lo.x, hi.y - lo.y);
        if (length > 0) for (auto& m : metrics) { m.start /= length; m.end /= length; }
        metrics.front().start = 0;
        metrics.back().end = 1;
    }
    std::size_t index(double u) const noexcept {
        auto it = std::ranges::upper_bound(metrics, u, {}, &Metric::end);
        return it == metrics.end() ? metrics.size() - 1 : static_cast<std::size_t>(it - metrics.begin());
    }
    double parameter(std::size_t index, double u) const noexcept {
        const auto& m = metrics[index];
        if (u <= m.start) return 0;
        if (u >= m.end) return 1;
        const double distance = (u - m.start) / (m.end - m.start) * m.length;
        auto first = m.lengths.begin(), last = first + static_cast<std::ptrdiff_t>(samples + 1);
        auto it = std::upper_bound(first, last, distance);
        const auto i = std::clamp(static_cast<unsigned>(it - first), 1u, samples);
        const double low = m.lengths[i - 1], high = m.lengths[i];
        const double f = high > low ? (distance - low) / (high - low) : 0;
        return (static_cast<double>(i - 1) + f) / samples;
    }
    Vec2 point(double u) const noexcept {
        const auto i = index(u);
        return curves[i].point(parameter(i, u));
    }
    Vec2 normalized(double u) const noexcept {
        auto p = point(u);
        return {(p.x - center.x) / scale, (p.y - center.y) / scale};
    }
};
// Drop constant segments only; an end==start cubic with nonconstant controls
// can be an entire valid contour and is never silently erased.
void remove_constants(std::vector<ShapeCubic>& curves) {
    std::erase_if(curves, [](const auto& c) { return c.start == c.control1 && c.start == c.control2 && c.start == c.end; });
}
double phase_for(const Measured& a, const Measured& b, unsigned count) noexcept {
    auto cost = [&](double phase) {
        double result = 0;
        for (unsigned i = 0; i < count; ++i) {
            const double u = static_cast<double>(i) / count;
            double v = u + phase;
            if (v >= 1) v -= 1;
            const auto p = a.normalized(u), q = b.normalized(v);
            const double dx = p.x - q.x, dy = p.y - q.y;
            result += dx * dx + dy * dy;
        }
        return result;
    };
    double best = 0, score = cost(0);
    auto consider = [&](double phase) {
        const double candidate = cost(phase);
        if (candidate < score) { score = candidate; best = phase; }
    };
    for (const auto& m : b.metrics) consider(m.start);
    for (unsigned i = 1; i < count; ++i) consider(static_cast<double>(i) / count);
    return best;
}
std::vector<ShapeCubic> rotate_start(const Measured& measured, double phase) {
    const auto index = measured.index(phase);
    const double t = measured.parameter(index, phase);
    std::vector<ShapeCubic> result;
    result.reserve(measured.curves.size() + 1);
    if (t > 0 && t < 1) result.push_back(split_shape(measured.curves[index], t).second);
    else if (t == 0) result.push_back(measured.curves[index]);
    for (std::size_t i = index + 1; i < measured.curves.size(); ++i) result.push_back(measured.curves[i]);
    for (std::size_t i = 0; i < index; ++i) result.push_back(measured.curves[i]);
    if (t > 0 && t < 1) result.push_back(split_shape(measured.curves[index], t).first);
    else if (t == 1) result.push_back(measured.curves[index]);
    return result;
}

} // namespace

Vec2 ShapeCubic::point(double parameter) const noexcept {
    const auto a = shape_mix(start, control1, parameter), b = shape_mix(control1, control2, parameter);
    const auto c = shape_mix(control2, end, parameter);
    return shape_mix(shape_mix(a, b, parameter), shape_mix(b, c, parameter), parameter);
}

std::expected<PreparedShapeMorph, ShapeMorphError>
PreparedShapeMorph::prepare(ShapeContour from, ShapeContour to, ShapeMorphOptions options) {
    if (options.max_input_segments < 1 || options.max_input_segments > 4096 ||
        options.max_output_segments < 1 || options.max_output_segments > 16384 ||
        options.arc_samples < 4 || options.arc_samples > 256 ||
        options.alignment_samples < 4 || options.alignment_samples > 128 || !options.point_path.valid())
        return std::unexpected(ShapeMorphError::invalid_options);
    auto a = cubics(from, options.max_input_segments), b = cubics(to, options.max_input_segments);
    if (!a) return std::unexpected(a.error());
    if (!b) return std::unexpected(b.error());
    remove_constants(*a); remove_constants(*b);
    if (a->empty() || b->empty()) return std::unexpected(ShapeMorphError::degenerate_contour);
    const double aa = area(*a), ab = area(*b);
    Measured ma(*a, options.arc_samples), mb(*b, options.arc_samples);
    auto valid = [](const Measured& m, double signed_area) -> std::expected<void, ShapeMorphError> {
        if (!std::isfinite(m.length) || !std::isfinite(signed_area))
            return std::unexpected(ShapeMorphError::numerical_range);
        if (!(m.length > 0) || !(m.scale > 0) || std::abs(signed_area) <= 1e-12)
            return std::unexpected(ShapeMorphError::degenerate_contour);
        for (const auto& metric : m.metrics)
            if (!(metric.length > 0) || !(metric.end > metric.start))
                return std::unexpected(ShapeMorphError::numerical_range);
        return {};
    };
    if (auto ok = valid(ma, aa); !ok) return std::unexpected(ok.error());
    if (auto ok = valid(mb, ab); !ok) return std::unexpected(ok.error());
    PreparedShapeMorph result;
    result.point_path_ = options.point_path;
    if (options.align_direction && ((aa < 0) != (ab < 0))) {
        reverse(*b);
        result.reversed_ = true;
        mb = Measured(*b, options.arc_samples);
    }
    if (auto ok = valid(mb, ab); !ok) return std::unexpected(ok.error());
    result.phase_ = options.align_start ? phase_for(ma, mb, options.alignment_samples) : 0;
    if (result.phase_ != 0) {
        *b = rotate_start(mb, result.phase_);
        mb = Measured(*b, options.arc_samples);
    }
    if (auto ok = valid(mb, ab); !ok) return std::unexpected(ok.error());
    std::vector<double> boundaries{0, 1};
    boundaries.reserve(ma.metrics.size() + mb.metrics.size() + 2);
    for (const auto& m : ma.metrics) boundaries.push_back(m.end);
    for (const auto& m : mb.metrics) boundaries.push_back(m.end);
    std::ranges::sort(boundaries);
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    if (boundaries.size() - 1 > options.max_output_segments)
        return std::unexpected(ShapeMorphError::segment_budget);
    result.from_.reserve(boundaries.size() - 1); result.to_.reserve(boundaries.size() - 1);
    auto append = [](std::vector<ShapeCubic>& output, const Measured& m, double lo, double hi) {
        const auto index = m.index(lo);
        output.push_back(portion(m.curves[index], m.parameter(index, lo), m.parameter(index, hi)));
    };
    for (std::size_t i = 1; i < boundaries.size(); ++i) {
        append(result.from_, ma, boundaries[i - 1], boundaries[i]);
        append(result.to_, mb, boundaries[i - 1], boundaries[i]);
    }
    // Shared vertices make closure bit-exact, including rounding at split seams.
    for (auto* curves : {&result.from_, &result.to_}) {
        for (std::size_t i = 1; i < curves->size(); ++i) (*curves)[i].start = (*curves)[i - 1].end;
        curves->back().end = curves->front().start;
    }
    return result;
}

std::expected<std::size_t, ShapeMorphError>
PreparedShapeMorph::sample(double progress, std::span<ShapeCubic> output) const noexcept {
    if (from_.empty()) return std::unexpected(ShapeMorphError::unprepared);
    if (!std::isfinite(progress)) return std::unexpected(ShapeMorphError::invalid_progress);
    if (output.size() < from_.size()) return std::unexpected(ShapeMorphError::output_too_small);
    const double t = std::clamp(progress, 0.0, 1.0);
    if (point_path_.linear()) {
        // Ordinary morphs keep the original direct loop; a process modifier
        // must not add a dispatch/call to every point of the default path.
        for (std::size_t i = 0; i < from_.size(); ++i) {
            const auto& a = from_[i]; const auto& b = to_[i];
            output[i] = {shape_mix(a.start, b.start, t), shape_mix(a.control1, b.control1, t),
                         shape_mix(a.control2, b.control2, t), shape_mix(a.end, b.end, t)};
        }
        return from_.size();
    }
    for (std::size_t i = 0; i < from_.size(); ++i) {
        const auto& a = from_[i]; const auto& b = to_[i];
        output[i] = {point_path_.interpolate(a.start, b.start, t), point_path_.interpolate(a.control1, b.control1, t),
                     point_path_.interpolate(a.control2, b.control2, t), point_path_.interpolate(a.end, b.end, t)};
    }
    return from_.size();
}

std::expected<ShapeCubic, ShapeMorphError>
PreparedShapeMorph::sample_segment(double progress, std::size_t segment) const noexcept {
    if (from_.empty()) return std::unexpected(ShapeMorphError::unprepared);
    if (!std::isfinite(progress)) return std::unexpected(ShapeMorphError::invalid_progress);
    if (segment >= from_.size()) return std::unexpected(ShapeMorphError::invalid_segment);
    const double t = std::clamp(progress, 0.0, 1.0);
    const auto& a = from_[segment]; const auto& b = to_[segment];
    if (point_path_.linear())
        return ShapeCubic{shape_mix(a.start, b.start, t), shape_mix(a.control1, b.control1, t),
                          shape_mix(a.control2, b.control2, t), shape_mix(a.end, b.end, t)};
    return ShapeCubic{point_path_.interpolate(a.start, b.start, t), point_path_.interpolate(a.control1, b.control1, t),
                      point_path_.interpolate(a.control2, b.control2, t), point_path_.interpolate(a.end, b.end, t)};
}

} // namespace hanim
