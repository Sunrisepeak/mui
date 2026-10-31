module mgame.physics;

import std;

namespace mgame {

double length(V2 v) noexcept { return std::sqrt(dot(v, v)); }

V2 normalized(V2 v) noexcept {
    const double l = length(v);
    return l > 0 ? V2{v.x / l, v.y / l} : V2{};
}

bool overlaps(V2 p, double r, const Box& b) noexcept {
    const double cx = std::clamp(p.x, b.x, b.right()), cy = std::clamp(p.y, b.y, b.bottom());
    const double dx = p.x - cx, dy = p.y - cy;
    return dx * dx + dy * dy < r * r;
}

std::optional<Hit> sweep(V2 p, double r, V2 d, const Box& b) noexcept {
    // Already in it (or touching), going further in: met at once, on its nearest side.
    if (overlaps(p, r, b)) {
        const double cx = std::clamp(p.x, b.x, b.right()), cy = std::clamp(p.y, b.y, b.bottom());
        V2 n;
        if (cx != p.x || cy != p.y) {
            n = normalized(p - V2{cx, cy});  // outside the box, inside its reach: from the nearest point
        } else {
            // The centre inside the box: out by the nearest face.
            const double left = p.x - b.x, right = b.right() - p.x, top = p.y - b.y, bottom = b.bottom() - p.y;
            const double least = std::min({left, right, top, bottom});
            n = least == left ? V2{-1, 0} : least == right ? V2{1, 0} : least == top ? V2{0, -1} : V2{0, 1};
        }
        if (dot(d, n) < 0) return Hit{0, n};
        return std::nullopt;
    }
    std::optional<Hit> best;
    // A contact counts only moving into the side met (a graze along it, or away, is no hit: no zero-time bounce loop).
    auto take = [&](double t, V2 n) {
        if (t >= 0 && t <= 1 && dot(d, n) < 0 && (!best || t < best->t)) best = Hit{t, n};
    };
    // The faces, moved out by r: crossed while moving toward them, within the side's span.
    if (d.x > 0 && p.x <= b.x - r) {
        const double t = (b.x - r - p.x) / d.x, y = p.y + d.y * t;
        if (y >= b.y && y <= b.bottom()) take(t, {-1, 0});
    }
    if (d.x < 0 && p.x >= b.right() + r) {
        const double t = (b.right() + r - p.x) / d.x, y = p.y + d.y * t;
        if (y >= b.y && y <= b.bottom()) take(t, {1, 0});
    }
    if (d.y > 0 && p.y <= b.y - r) {
        const double t = (b.y - r - p.y) / d.y, x = p.x + d.x * t;
        if (x >= b.x && x <= b.right()) take(t, {0, -1});
    }
    if (d.y < 0 && p.y >= b.bottom() + r) {
        const double t = (b.bottom() + r - p.y) / d.y, x = p.x + d.x * t;
        if (x >= b.x && x <= b.right()) take(t, {0, 1});
    }
    // The corners: the path meets a circle of radius r about each (the first root).
    const double a = dot(d, d);
    if (a > 0)
        for (const V2 c : {V2{b.x, b.y}, V2{b.right(), b.y}, V2{b.x, b.bottom()}, V2{b.right(), b.bottom()}}) {
            const V2 m = p - c;
            const double half_b = dot(m, d), cc = dot(m, m) - r * r;
            const double disc = half_b * half_b - a * cc;
            if (disc < 0 || half_b >= 0) continue;  // missed, or moving away
            // The first root in the form that does not cancel near contact: c / (−b′ + √disc), with b′ < 0 here.
            const double t = cc / (-half_b + std::sqrt(disc));
            const V2 at = p + d * t;
            // Only where the corner is the nearest part of the box (beyond both faces' spans).
            const bool beyond_x = at.x < b.x || at.x > b.right(), beyond_y = at.y < b.y || at.y > b.bottom();
            if (beyond_x && beyond_y) take(t, normalized(at - c));
        }
    return best;
}

}  // namespace mgame
