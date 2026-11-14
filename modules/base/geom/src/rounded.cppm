// mlibs.geom:rounded — a rectangle with rounded corners as a path, once for
// every renderer (the CPU raster, the vector display list, SVG, the GPU's
// masks): written into any builder with move_to, line_to, cubic_to and close.
//
// Circular corners (smoothing 0) are a quarter circle as one cubic each.
// Smoothed corners keep the curvature continuous where the edge meets the
// corner (Figma's corner smoothing): each starts `p = (1 + s)·r` from the
// corner, eases in on a cubic, follows a circular arc shortened to
// 90°·(1 − s), and eases out on the mirrored cubic; s is limited so that p
// stays within half the shorter side. mlibs.math's deterministic functions
// make it the same bits everywhere.
export module mlibs.geom.api:rounded;

import std;
import mlibs.math;
import :primitives;

export namespace mlibs::geom {

namespace rounded_detail {
// One smoothed corner's lengths: along each edge `p` in all, the cubic's
// `a`, `b`, `c`, `d` (as Figma names them), the arc's chord per axis and its sweep.
struct Corner {
    float p = 0, a = 0, b = 0, c = 0, d = 0, chord = 0, sweep = 0;
};
inline Corner corner(float radius, float smoothing, float budget) noexcept {
    namespace det = mlibs::math::det;
    double s = std::clamp(static_cast<double>(smoothing), 0.0, 1.0);
    double p = (1 + s) * radius;
    if (radius > 0) s = std::clamp(s, 0.0, std::max(0.0, static_cast<double>(budget) / radius - 1));
    p = std::min(p, static_cast<double>(budget));
    const double sweep = det::pi / 2 * (1 - s);
    const double chord = det::sin(sweep / 2) * radius * std::numbers::sqrt2;
    const double alpha = (det::pi / 2 - sweep) / 2;
    const double reach = radius * det::sin(alpha / 2) / det::cos(alpha / 2);
    const double beta = det::pi / 4 * s;
    const double c = reach * det::cos(beta), d = c * det::sin(beta) / det::cos(beta);
    const double b = (p - chord - c - d) / 3, a = 2 * b;
    return {static_cast<float>(p), static_cast<float>(a), static_cast<float>(b), static_cast<float>(c), static_cast<float>(d),
            static_cast<float>(chord), static_cast<float>(sweep)};
}
// A circular arc from `from` to `to` about `centre`, sweeping `sweep`, as one cubic.
template <class Out>
void arc(Out& out, Point from, Point to, Point centre, float sweep) {
    namespace det = mlibs::math::det;
    const float handle = static_cast<float>(4.0 / 3.0 * det::sin(sweep / 4.0) / det::cos(sweep / 4.0));
    const Point t0{-(from.y - centre.y), from.x - centre.x}, t1{-(to.y - centre.y), to.x - centre.x};
    out.cubic_to({from.x + handle * t0.x, from.y + handle * t0.y}, {to.x - handle * t1.x, to.y - handle * t1.y}, to);
}
}  // namespace rounded_detail

template <class Out>
void rounded_rect(Out& out, const Rect& r, const CornerRadii& radii) {
    const float l = r.x, t = r.y, rr = r.right(), b = r.bottom();
    if (radii.zero()) {
        out.move_to({l, t});
        out.line_to({rr, t});
        out.line_to({rr, b});
        out.line_to({l, b});
        out.close();
        return;
    }
    const CornerRadii f = radii.fitted(r.size());
    if (f.smoothing <= 0) {
        constexpr float k = 0.5522847498307936f;  // 4/3·(√2 − 1): a quarter circle as one cubic
        const float tl = f.top_left, tr = f.top_right, br = f.bottom_right, bl = f.bottom_left;
        out.move_to({l + tl, t});
        out.line_to({rr - tr, t});
        if (tr > 0) out.cubic_to({rr - tr + k * tr, t}, {rr, t + tr - k * tr}, {rr, t + tr});
        out.line_to({rr, b - br});
        if (br > 0) out.cubic_to({rr, b - br + k * br}, {rr - br + k * br, b}, {rr - br, b});
        out.line_to({l + bl, b});
        if (bl > 0) out.cubic_to({l + bl - k * bl, b}, {l, b - bl + k * bl}, {l, b - bl});
        out.line_to({l, t + tl});
        if (tl > 0) out.cubic_to({l, t + tl - k * tl}, {l + tl - k * tl, t}, {l + tl, t});
        out.close();
        return;
    }
    using rounded_detail::corner;
    const float budget = std::min(r.width, r.height) / 2;
    const auto tr = corner(f.top_right, f.smoothing, budget), br = corner(f.bottom_right, f.smoothing, budget);
    const auto bl = corner(f.bottom_left, f.smoothing, budget), tl = corner(f.top_left, f.smoothing, budget);
    out.move_to({l + tl.p, t});
    // Each corner clockwise: along the edge, the easing cubic, the arc, the easing cubic out.
    out.line_to({rr - tr.p, t});
    if (f.top_right > 0) {
        const float x = rr - tr.p, y = t;
        out.cubic_to({x + tr.a, y}, {x + tr.a + tr.b, y}, {x + tr.a + tr.b + tr.c, y + tr.d});
        const Point s{x + tr.a + tr.b + tr.c, y + tr.d}, e{s.x + tr.chord, s.y + tr.chord};
        rounded_detail::arc(out, s, e, {rr - f.top_right, t + f.top_right}, tr.sweep);
        out.cubic_to({e.x + tr.d, e.y + tr.c}, {e.x + tr.d, e.y + tr.b + tr.c}, {rr, t + tr.p});
    }
    out.line_to({rr, b - br.p});
    if (f.bottom_right > 0) {
        const float x = rr, y = b - br.p;
        out.cubic_to({x, y + br.a}, {x, y + br.a + br.b}, {x - br.d, y + br.a + br.b + br.c});
        const Point s{x - br.d, y + br.a + br.b + br.c}, e{s.x - br.chord, s.y + br.chord};
        rounded_detail::arc(out, s, e, {rr - f.bottom_right, b - f.bottom_right}, br.sweep);
        out.cubic_to({e.x - br.c, e.y + br.d}, {e.x - br.b - br.c, e.y + br.d}, {rr - br.p, b});
    }
    out.line_to({l + bl.p, b});
    if (f.bottom_left > 0) {
        const float x = l + bl.p, y = b;
        out.cubic_to({x - bl.a, y}, {x - bl.a - bl.b, y}, {x - bl.a - bl.b - bl.c, y - bl.d});
        const Point s{x - bl.a - bl.b - bl.c, y - bl.d}, e{s.x - bl.chord, s.y - bl.chord};
        rounded_detail::arc(out, s, e, {l + f.bottom_left, b - f.bottom_left}, bl.sweep);
        out.cubic_to({e.x - bl.d, e.y - bl.c}, {e.x - bl.d, e.y - bl.b - bl.c}, {l, b - bl.p});
    }
    out.line_to({l, t + tl.p});
    if (f.top_left > 0) {
        const float x = l, y = t + tl.p;
        out.cubic_to({x, y - tl.a}, {x, y - tl.a - tl.b}, {x + tl.d, y - tl.a - tl.b - tl.c});
        const Point s{x + tl.d, y - tl.a - tl.b - tl.c}, e{s.x + tl.chord, s.y - tl.chord};
        rounded_detail::arc(out, s, e, {l + f.top_left, t + f.top_left}, tl.sweep);
        out.cubic_to({e.x + tl.c, e.y - tl.d}, {e.x + tl.b + tl.c, e.y - tl.d}, {l + tl.p, t});
    }
    out.close();
}

}  // namespace mlibs::geom
