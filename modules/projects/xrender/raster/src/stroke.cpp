module xrender.raster.api;

import std;
import mlibs.math;

namespace xrender::raster {

namespace {

namespace dm = mlibs::math::det;

struct Polyline {
    std::vector<Point> points;
    bool               closed = false;
};

float dist(Point a, Point b) { return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)); }

// Signed area ×2 (positive: counter-clockwise in a y-up sense, clockwise on screen).
float area2(std::span<const Point> p) {
    float a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Point& u = p[i];
        const Point& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a;
}

void emit(Polygons& out, std::vector<Point>& poly) {
    if (poly.size() < 3) return;
    if (area2(poly) < 0) std::ranges::reverse(poly);
    out.begin();
    out.points.insert(out.points.end(), poly.begin(), poly.end());
    out.closed.back() = true;
}

// A loop as it was walked: its orientation is what makes a band (stroke_outline).
void close_into(Polygons& out, const std::vector<Point>& poly) {
    if (poly.size() < 3) return;
    out.begin();
    out.points.insert(out.points.end(), poly.begin(), poly.end());
    out.closed.back() = true;
}

// The angle an arc of radius h may turn per chord within `tolerance`:
// 2·acos(1 − tolerance / h) — the same for every arc of one stroke.
double arc_step(float h, float tolerance) {
    double ratio = std::clamp(1.0 - tolerance / std::max(h, 1e-3f), -1.0, 1.0);
    return 2.0 * dm::atan2(std::sqrt(1.0 - ratio * ratio), ratio);
}

// Points of an arc of radius h around c from angle a0 sweeping `sweep`, a
// chord at most `step` (arc_step) of turn.
void arc(std::vector<Point>& out, Point c, float h, double a0, double sweep, double step) {
    int n = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / std::max(step, 1e-3))));
    n = std::min(n, 256);
    for (int i = 0; i <= n; ++i) {
        double a = a0 + sweep * static_cast<double>(i) / n;
        out.push_back({static_cast<float>(c.x + h * dm::cos(a)), static_cast<float>(c.y + h * dm::sin(a))});
    }
}

std::vector<Polyline> dash(const std::vector<Polyline>& lines, std::span<const float> pattern, float offset) {
    double total = 0;
    for (float d : pattern) {
        // This low-level API has no CSS declaration to reject. Ignore invalid
        // dashing as a whole, retaining the solid stroke, rather than walking
        // negative/NaN lengths indefinitely or emitting nonfinite geometry.
        if (!std::isfinite(d) || d < 0) return lines;
        total += d;
    }
    if (pattern.empty() || total <= 0) return lines;
    // Odd lists repeat twice before the on/off state repeats. Keep a logical
    // even cycle; no extra allocation or copied dash pattern is needed.
    const std::size_t cycle = pattern.size() + (pattern.size() % 2 ? pattern.size() : 0);
    if (pattern.size() % 2) total *= 2;
    double phase = std::isfinite(offset) ? dm::fmod(static_cast<double>(offset), total) : 0;
    if (phase < 0) phase += total;
    if (phase >= total) phase = 0; // rounding when a tiny negative phase is added to a large total
    std::size_t first = 0;
    while (first < cycle && phase >= pattern[first % pattern.size()]) {
        phase -= pattern[first % pattern.size()];
        ++first;
    }
    if (first == cycle) first = 0, phase = 0;
    std::vector<Polyline> out;
    for (const Polyline& l : lines) {
        std::vector<Point> pts = l.points;
        if (l.closed && !pts.empty()) pts.push_back(pts.front());
        // Position in the pattern at the start.
        std::size_t k = first;
        float left = static_cast<float>(pattern[k % pattern.size()] - phase);
        bool on = k % 2 == 0;
        Polyline cur;
        if (on && !pts.empty()) cur.points.push_back(pts[0]);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            Point a = pts[i], b = pts[i + 1];
            float len = dist(a, b), pos = 0;
            while (len - pos > left) {
                pos += left;
                float t = len > 0 ? pos / len : 0;
                Point p{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
                if (on) {
                    cur.points.push_back(p);
                    out.push_back(std::move(cur));
                    cur = {};
                } else {
                    cur.points = {p};
                }
                on = !on;
                k = (k + 1) % cycle;
                left = std::max(pattern[k % pattern.size()], 1e-4f);
            }
            left -= len - pos;
            if (on) cur.points.push_back(b);
        }
        if (on && cur.points.size() >= 2) out.push_back(std::move(cur));
    }
    return out;
}

// The stretch of `line` from distance `from` to `to` along it, `to` past
// its length wrapping round a closed line.
void stretch(const Polyline& line, double from, double to, std::vector<Polyline>& out) {
    const auto& points = line.points;
    const std::size_t corners = points.size();
    const std::size_t segments = line.closed ? corners : corners - 1;
    Polyline piece;
    double walked = 0;
    // Twice round a closed line covers any wrapped stretch.
    const std::size_t steps = line.closed ? 2 * segments : segments;
    for (std::size_t step = 0; step < steps && walked < to; ++step) {
        const Point& head = points[step % corners];
        const Point& tail = points[(step + 1) % corners];
        const double length = dist(head, tail);
        const double next = walked + length;
        if (next > from && length > 0) {
            const auto at = [&](double distance) {
                const float along = static_cast<float>(std::clamp((distance - walked) / length, 0.0, 1.0));
                return Point{head.x + (tail.x - head.x) * along, head.y + (tail.y - head.y) * along};
            };
            if (piece.points.empty()) piece.points.push_back(at(std::max(from, walked)));
            piece.points.push_back(next <= to ? tail : at(to));
        }
        walked = next;
    }
    if (!piece.points.empty()) out.push_back(std::move(piece));
}

std::vector<Polyline> trim(const std::vector<Polyline>& lines, float start, float end, float offset) {
    const auto finite_or = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    double first = std::clamp(finite_or(start, 0), 0.0f, 1.0f);
    double last = std::clamp(finite_or(end, 1), 0.0f, 1.0f);
    if (first > last) std::swap(first, last);
    const double shift = finite_or(offset, 0);
    if (last - first >= 1) return lines;
    std::vector<Polyline> out;
    if (last - first <= 0) return out;
    for (const Polyline& line : lines) {
        if (line.points.size() < 2) continue;
        double length = 0;
        const std::size_t corners = line.points.size();
        const std::size_t segments = line.closed ? corners : corners - 1;
        for (std::size_t index = 0; index < segments; ++index) length += dist(line.points[index], line.points[(index + 1) % corners]);
        if (!(length > 0)) continue;
        double from = first + shift;
        from -= std::floor(from);
        const double to = from + (last - first);
        if (line.closed || to <= 1) stretch(line, from * length, to * length, out);
        else {  // an open line splits where the stretch passes its end
            stretch(line, from * length, length, out);
            stretch(line, 0, (to - 1) * length, out);
        }
    }
    return out;
}

}  // namespace

void stroke_outline(PathData path, const Affine& m, const Stroke& stroke, Polygons& out) {
    float h = stroke.width / 2;
    if (!(h > 0)) return;
    float scale = m.max_scale();
    float tolerance = (1.0f / 32.0f) / (scale > 0 ? scale : 1.0f);

    Polygons flat;
    flatten(path, Affine{}, flat, tolerance);
    std::vector<Polyline> lines;
    for (std::size_t i = 0; i < flat.count(); ++i) {
        Polyline pl;
        pl.closed = flat.closed[i];
        for (const Point& p : flat.polygon(i))
            if (pl.points.empty() || dist(pl.points.back(), p) > 1e-6f) pl.points.push_back(p);
        if (pl.closed && pl.points.size() > 1 && dist(pl.points.front(), pl.points.back()) <= 1e-6f) pl.points.pop_back();
        lines.push_back(std::move(pl));
    }
    if (stroke.trim_start != 0 || stroke.trim_end != 1 || stroke.trim_offset != 0)
        lines = trim(lines, stroke.trim_start, stroke.trim_end, stroke.trim_offset);
    if (!stroke.dashes.empty()) lines = dash(lines, stroke.dashes, stroke.dash_offset);

    const double step = arc_step(h, tolerance);
    Polygons local;
    std::vector<Point> poly;
    std::vector<Point> scratch_normals;
    std::array<std::vector<double>, 2> angles;
    std::vector<float> scratch_lengths;
    for (const Polyline& pl : lines) {
        const auto& p = pl.points;
        if (p.empty()) continue;
        if (p.size() == 1) {  // a dot: only round and square caps draw it
            poly.clear();
            if (stroke.cap == Cap::round) arc(poly, p[0], h, 0, dm::two_pi, step);
            else if (stroke.cap == Cap::square)
                poly = {{p[0].x - h, p[0].y - h}, {p[0].x + h, p[0].y - h}, {p[0].x + h, p[0].y + h}, {p[0].x - h, p[0].y + h}};
            emit(local, poly);
            continue;
        }
        const std::size_t n = p.size();
        const std::size_t segments = pl.closed ? n : n - 1;
        // The left normal of each segment, h long.
        std::vector<Point>& normals = scratch_normals;
        normals.clear();
        std::vector<float>& lengths = scratch_lengths;
        lengths.clear();
        for (std::size_t i = 0; i < segments; ++i) {
            const Point a = p[i], b = p[(i + 1) % n];
            const float length = dist(a, b);
            lengths.push_back(length);
            normals.push_back({-(b.y - a.y) / length * h, (b.x - a.x) / length * h});
        }
        // A segment's normal's angle on either side, each found once: a round
        // join at either end of a segment asks it with the same numbers.
        angles[0].assign(segments, std::numeric_limits<double>::quiet_NaN());
        angles[1].assign(segments, std::numeric_limits<double>::quiet_NaN());
        const auto angle = [&](std::size_t segment, float sign) {
            double& known = angles[sign > 0 ? 0 : 1][segment];
            if (known != known) known = dm::atan2(sign * normals[segment].y, sign * normals[segment].x);
            return known;
        };
        // One side's offset at the vertex between segments `in` and `out`
        // (side +1: the left normals', −1: the right), in the order the
        // side is walked: the outer side of a turn gets the join, the inner
        // side the point where its offset lines meet — or, where the
        // segments are too short for that, the vertex itself between them
        // (a pivot, as Skia's stroker does), which keeps the winding whole.
        const auto corner = [&](std::vector<Point>& side, std::size_t vertex, std::size_t in, std::size_t out_segment, float sign) {
            const Point c = p[vertex];
            const Point n0{sign * normals[in].x, sign * normals[in].y}, n1{sign * normals[out_segment].x, sign * normals[out_segment].y};
            const Point a{c.x + n0.x, c.y + n0.y}, b{c.x + n1.x, c.y + n1.y};
            const float cross = normals[in].x * normals[out_segment].y - normals[in].y * normals[out_segment].x;
            const float dot = n0.x * n1.x + n0.y * n1.y;
            if (std::abs(cross) <= 1e-6f * h * h && dot > 0) {  // straight on
                side.push_back(a);
                return;
            }
            // The left side is outer where the path turns right (cross < 0);
            // where it turns back on itself, the left side takes the join.
            const bool reversal = std::abs(cross) <= 1e-6f * h * h;
            const bool outer = reversal ? sign > 0 : sign * cross < 0;
            const float k = h * h + dot;
            if (!outer) {
                // The inner offset lines meet h·tan(θ/2) along both segments
                // from the vertex; within half of each, no two corners cross.
                const float along = k > 1e-6f ? h * std::abs(cross) / k : std::numeric_limits<float>::infinity();
                if (along <= 0.5f * std::min(lengths[in], lengths[out_segment])) {
                    side.push_back({c.x + (n0.x + n1.x) * h * h / k, c.y + (n0.y + n1.y) * h * h / k});
                } else {
                    side.push_back(a);
                    side.push_back(c);
                    side.push_back(b);
                }
                return;
            }
            switch (stroke.join) {
            case Join::round: {
                const double a0 = angle(in, sign), a1 = angle(out_segment, sign);
                double sweep = a1 - a0;
                while (sweep > dm::pi) sweep -= dm::two_pi;
                while (sweep < -dm::pi) sweep += dm::two_pi;
                if (reversal) {  // half a turn, round the far side of the vertex
                    const Point travel{normals[in].y, -normals[in].x};
                    sweep = n0.x * travel.y - n0.y * travel.x > 0 ? dm::pi : -dm::pi;
                }
                arc(side, c, h, a0, sweep, step);
                break;
            }
            case Join::miter:
                if (k > 1e-6f) {
                    const Point miter{c.x + (n0.x + n1.x) * h * h / k, c.y + (n0.y + n1.y) * h * h / k};
                    if (dist(c, miter) <= stroke.miter_limit * h) {
                        side.push_back(miter);
                        break;
                    }
                }
                [[fallthrough]];
            case Join::bevel:
                side.push_back(a);
                side.push_back(b);
                break;
            }
        };
        if (pl.closed) {
            // Two loops: the left side forward and the right side back, so
            // the band between them winds once and the inside not at all.
            poly.clear();
            for (std::size_t v = 0; v < n; ++v) corner(poly, v, (v + n - 1) % n, v, 1);
            close_into(local, poly);
            poly.clear();
            for (std::size_t v = n; v-- > 0;) {
                const std::size_t before = poly.size();
                corner(poly, v, (v + n - 1) % n, v, -1);
                std::reverse(poly.begin() + static_cast<std::ptrdiff_t>(before), poly.end());
            }
            close_into(local, poly);
            continue;
        }
        // One loop: the left side forward, the end cap, the right side back,
        // the start cap.
        const auto cap = [&](Point c, Point normal, Point outward) {
            switch (stroke.cap) {
            case Cap::butt:
                poly.push_back({c.x + normal.x, c.y + normal.y});
                poly.push_back({c.x - normal.x, c.y - normal.y});
                break;
            case Cap::square:
                poly.push_back({c.x + normal.x, c.y + normal.y});
                poly.push_back({c.x + normal.x + outward.x, c.y + normal.y + outward.y});
                poly.push_back({c.x - normal.x + outward.x, c.y - normal.y + outward.y});
                poly.push_back({c.x - normal.x, c.y - normal.y});
                break;
            case Cap::round: {
                // From +normal to −normal through the outward direction.
                const double a0 = dm::atan2(normal.y, normal.x);
                const float turn = normal.x * outward.y - normal.y * outward.x;
                arc(poly, c, h, a0, turn > 0 ? dm::pi : -dm::pi, step);
                break;
            }
            }
        };
        const auto direction = [&](std::size_t segment) {  // h long, along the segment
            return Point{normals[segment].y, -normals[segment].x};
        };
        poly.clear();
        poly.push_back({p[0].x + normals[0].x, p[0].y + normals[0].y});
        for (std::size_t v = 1; v + 1 < n; ++v) corner(poly, v, v - 1, v, 1);
        const Point forward = direction(segments - 1);
        cap(p[n - 1], normals[segments - 1], forward);
        for (std::size_t v = n - 1; v-- > 1;) {
            const std::size_t before = poly.size();
            corner(poly, v, v - 1, v, -1);
            std::reverse(poly.begin() + static_cast<std::ptrdiff_t>(before), poly.end());
        }
        const Point backward = direction(0);
        cap(p[0], Point{-normals[0].x, -normals[0].y}, Point{-backward.x, -backward.y});
        close_into(local, poly);
    }
    for (std::size_t i = 0; i < local.count(); ++i) {
        out.begin();
        for (const Point& q : local.polygon(i)) out.points.push_back(m.apply(q));
        out.closed.back() = true;
    }
}

void stroke_coverage(PathData path, const Stroke& stroke, const Affine& m, PixelRect clip, Coverage& out) {
    Polygons polygons;
    stroke_outline(path, m, stroke, polygons);
    Rasterizer r;
    r.rasterize(polygons, FillRule::nonzero, clip, out);
}

}  // namespace xrender::raster
