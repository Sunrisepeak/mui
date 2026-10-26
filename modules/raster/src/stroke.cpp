module openmui.raster;

import std;
import openmui.anim;

namespace omui::raster {

namespace {

namespace dm = anim::dm;

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

// Points of an arc of radius h around c from angle a0 sweeping `sweep`.
void arc(std::vector<Point>& out, Point c, float h, double a0, double sweep, float tolerance) {
    double ratio = std::clamp(1.0 - tolerance / std::max(h, 1e-3f), -1.0, 1.0);
    double step = 2.0 * dm::atan2(std::sqrt(1.0 - ratio * ratio), ratio);  // 2·acos(ratio)
    int n = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / std::max(step, 1e-3))));
    n = std::min(n, 256);
    for (int i = 0; i <= n; ++i) {
        double a = a0 + sweep * static_cast<double>(i) / n;
        out.push_back({static_cast<float>(c.x + h * dm::cos(a)), static_cast<float>(c.y + h * dm::sin(a))});
    }
}

std::vector<Polyline> dash(const std::vector<Polyline>& lines, std::span<const float> pattern, float offset) {
    float total = 0;
    for (float d : pattern) total += std::max(0.0f, d);
    if (pattern.empty() || total <= 0) return lines;
    std::vector<Polyline> out;
    for (const Polyline& l : lines) {
        std::vector<Point> pts = l.points;
        if (l.closed && !pts.empty()) pts.push_back(pts.front());
        // Position in the pattern at the start.
        float phase = offset - total * std::floor(offset / total);
        std::size_t k = 0;
        while (phase >= pattern[k % pattern.size()]) {
            phase -= pattern[k % pattern.size()];
            ++k;
        }
        float left = pattern[k % pattern.size()] - phase;
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
                k = (k + 1) % pattern.size();
                left = std::max(pattern[k], 1e-4f);
            }
            left -= len - pos;
            if (on) cur.points.push_back(b);
        }
        if (on && cur.points.size() >= 2) out.push_back(std::move(cur));
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
    if (!stroke.dashes.empty()) lines = dash(lines, stroke.dashes, stroke.dash_offset);

    Polygons local;
    std::vector<Point> poly;
    for (const Polyline& pl : lines) {
        const auto& p = pl.points;
        if (p.empty()) continue;
        if (p.size() == 1) {  // a dot: only round and square caps draw it
            poly.clear();
            if (stroke.cap == Cap::round) arc(poly, p[0], h, 0, dm::two_pi, tolerance);
            else if (stroke.cap == Cap::square)
                poly = {{p[0].x - h, p[0].y - h}, {p[0].x + h, p[0].y - h}, {p[0].x + h, p[0].y + h}, {p[0].x - h, p[0].y + h}};
            emit(local, poly);
            continue;
        }
        std::size_t n = p.size();
        std::size_t segs = pl.closed ? n : n - 1;
        auto normal = [&](std::size_t i) {
            Point a = p[i], b = p[(i + 1) % n];
            float len = dist(a, b);
            return Point{-(b.y - a.y) / len * h, (b.x - a.x) / len * h};
        };
        for (std::size_t i = 0; i < segs; ++i) {
            Point a = p[i], b = p[(i + 1) % n], nm = normal(i);
            poly = {{a.x + nm.x, a.y + nm.y}, {b.x + nm.x, b.y + nm.y}, {b.x - nm.x, b.y - nm.y}, {a.x - nm.x, a.y - nm.y}};
            emit(local, poly);
        }
        // Joins at every vertex between two segments.
        std::size_t first_join = pl.closed ? 0 : 1, last_join = pl.closed ? n : n - 1;
        for (std::size_t v = first_join; v < last_join; ++v) {
            std::size_t in = (v + n - 1) % n, out_seg = v;
            Point c = p[v];
            Point n0 = normal(in), n1 = normal(out_seg);
            float cross = n0.x * n1.y - n0.y * n1.x;
            if (std::abs(cross) < 1e-9f && n0.x * n1.x + n0.y * n1.y > 0) continue;  // straight on
            // The outer side is where the offsets separate.
            float sgn = cross > 0 ? -1.0f : 1.0f;
            Point a{c.x + sgn * n0.x, c.y + sgn * n0.y}, b{c.x + sgn * n1.x, c.y + sgn * n1.y};
            poly.clear();
            if (stroke.join == Join::round) {
                double a0 = dm::atan2(a.y - c.y, a.x - c.x), a1 = dm::atan2(b.y - c.y, b.x - c.x);
                double sweep = a1 - a0;
                while (sweep > dm::pi) sweep -= dm::two_pi;
                while (sweep < -dm::pi) sweep += dm::two_pi;
                poly.push_back(c);
                arc(poly, c, h, a0, sweep, tolerance);
            } else {
                poly = {c, a, b};
                if (stroke.join == Join::miter) {
                    // Miter point: c + sgn·(n0 + n1)·h²/(h² + n0·n1)
                    float dot = n0.x * n1.x + n0.y * n1.y;
                    float k = h * h + dot;
                    if (k > 1e-6f) {
                        Point mp{c.x + sgn * (n0.x + n1.x) * h * h / k, c.y + sgn * (n0.y + n1.y) * h * h / k};
                        float miter_len = dist(c, mp);
                        if (miter_len <= stroke.miter_limit * h) poly = {c, a, mp, b};
                    }
                }
            }
            emit(local, poly);
        }
        if (!pl.closed && stroke.cap != Cap::butt) {
            for (int end = 0; end < 2; ++end) {
                Point c = end == 0 ? p.front() : p.back();
                Point other = end == 0 ? p[1] : p[n - 2];
                float len = dist(other, c);
                Point d{(c.x - other.x) / len * h, (c.y - other.y) / len * h};  // outward along the line
                Point nm{-d.y, d.x};
                poly.clear();
                if (stroke.cap == Cap::square) {
                    poly = {{c.x + nm.x, c.y + nm.y}, {c.x + nm.x + d.x, c.y + nm.y + d.y},
                            {c.x - nm.x + d.x, c.y - nm.y + d.y}, {c.x - nm.x, c.y - nm.y}};
                } else {
                    double a0 = dm::atan2(nm.y, nm.x);
                    poly.push_back(c);
                    arc(poly, c, h, a0, -dm::pi, tolerance);
                }
                emit(local, poly);
            }
        }
    }
    for (std::size_t i = 0; i < local.count(); ++i) {
        out.begin();
        for (const Point& q : local.polygon(i)) out.points.push_back(m.apply(q));
        out.closed.back() = true;
    }
}

}  // namespace omui::raster
