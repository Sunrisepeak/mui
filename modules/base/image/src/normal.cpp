// normal_map: a picture's tangent-space normal map, in integers only (so the
// same bytes on every platform and compiler).
//
// The signed distance to the picture's edge (exact Euclidean, Meijster's two
// passes in squared integers, inside and outside) is smoothed by a box blur
// (sums kept exact, divided once at the end, so the result is symmetric
// under turns and mirrors), then lifted by a rounded bevel — a quarter
// circle — so the slope is steepest at the silhouette whatever the blur did
// to the ridges; painted folds (luminance, blurred the same) are added in
// proportion to the bevel's height. Sobel slopes and an integer square root
// make the unit normal.
module openmui.image;

import std;

namespace omui::imageio {

namespace {

// ⌊√v⌋, exactly.
std::uint64_t isqrt(std::uint64_t v) noexcept {
    if (v < 2) return v;
    std::uint64_t r = 0, bit = std::uint64_t{1} << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) noexcept {  // b > 0
    const std::int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

// a / b rounded to nearest (halves away from zero), b > 0.
std::int64_t round_div(std::int64_t a, std::int64_t b) noexcept { return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b); }

// A float option as a fixed-point integer (one exactly rounded product).
std::int64_t fixed(float v, float unit) noexcept { return std::llround(static_cast<double>(v) * static_cast<double>(unit)); }

// The squared distance from every pixel to the nearest target pixel (a
// target: 0), the picture surrounded by a border of one pixel that is a
// target or not. Meijster, Roerdink and Hesselink's linear-time exact
// algorithm; with no target at all, every distance is the same large value.
std::vector<std::int64_t> squared_distance(const std::vector<std::uint8_t>& target, int w, int h, bool border_is_target) {
    const int W = w + 2, H = h + 2;
    const std::int64_t far = 2 * (static_cast<std::int64_t>(W) + H);
    auto is_target = [&](int x, int y) {
        if (x < 1 || y < 1 || x > w || y > h) return border_is_target;
        return target[static_cast<std::size_t>(y - 1) * w + (x - 1)] != 0;
    };
    std::vector<std::int64_t> g(static_cast<std::size_t>(W) * H);
    for (int x = 0; x < W; ++x) {
        std::int64_t run = far;
        for (int y = 0; y < H; ++y) {
            run = is_target(x, y) ? 0 : std::min(run + 1, far);
            g[static_cast<std::size_t>(y) * W + x] = run;
        }
        for (int y = H - 2; y >= 0; --y) {
            std::int64_t& v = g[static_cast<std::size_t>(y) * W + x];
            v = std::min(v, g[static_cast<std::size_t>(y + 1) * W + x] + 1);
        }
    }
    std::vector<std::int64_t> dt(static_cast<std::size_t>(W) * H);
    std::vector<std::int64_t> s(static_cast<std::size_t>(W)), t(static_cast<std::size_t>(W));
    for (int y = 0; y < H; ++y) {
        const std::int64_t* gr = g.data() + static_cast<std::size_t>(y) * W;
        auto f = [&](std::int64_t x, std::int64_t i) { return (x - i) * (x - i) + gr[i] * gr[i]; };
        auto sep = [&](std::int64_t i, std::int64_t u) { return floor_div(u * u - i * i + gr[u] * gr[u] - gr[i] * gr[i], 2 * (u - i)); };
        std::int64_t q = 0;
        s[0] = 0;
        t[0] = 0;
        for (std::int64_t u = 1; u < W; ++u) {
            while (q >= 0 && f(t[static_cast<std::size_t>(q)], s[static_cast<std::size_t>(q)]) > f(t[static_cast<std::size_t>(q)], u)) --q;
            if (q < 0) {
                q = 0;
                s[0] = u;
            } else {
                const std::int64_t at = 1 + sep(s[static_cast<std::size_t>(q)], u);
                if (at < W) {
                    ++q;
                    s[static_cast<std::size_t>(q)] = u;
                    t[static_cast<std::size_t>(q)] = at;
                }
            }
        }
        for (std::int64_t u = W - 1; u >= 0; --u) {
            dt[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(u)] = f(u, s[static_cast<std::size_t>(q)]);
            if (u == t[static_cast<std::size_t>(q)]) --q;
        }
    }
    std::vector<std::int64_t> out(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) out[static_cast<std::size_t>(y) * w + x] = dt[static_cast<std::size_t>(y + 1) * W + (x + 1)];
    return out;
}

// A box sum of radius r along rows or columns, edges clamped: exact (no
// division), so two passes each way commute and the blur is symmetric.
void box_sum(std::vector<std::int64_t>& v, std::vector<std::int64_t>& tmp, int w, int h, int r, bool rows) {
    const int n = rows ? w : h, lines = rows ? h : w;
    tmp.resize(v.size());
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int i) -> std::int64_t {
            i = std::clamp(i, 0, n - 1);
            return rows ? v[static_cast<std::size_t>(l) * w + i] : v[static_cast<std::size_t>(i) * w + l];
        };
        std::int64_t sum = 0;
        for (int i = -r; i <= r; ++i) sum += at(i);
        for (int i = 0; i < n; ++i) {
            (rows ? tmp[static_cast<std::size_t>(l) * w + i] : tmp[static_cast<std::size_t>(i) * w + l]) = sum;
            sum += at(i + r + 1) - at(i - r);
        }
    }
    v.swap(tmp);
}

// Blurred by a box of radius r twice each way; the sums divided once, rounded.
void blur(std::vector<std::int64_t>& v, int w, int h, int r) {
    if (r <= 0) return;
    std::vector<std::int64_t> tmp;
    for (int pass = 0; pass < 2; ++pass) {
        box_sum(v, tmp, w, h, r, true);
        box_sum(v, tmp, w, h, r, false);
    }
    const std::int64_t k = 2 * static_cast<std::int64_t>(r) + 1, div = k * k * k * k;
    for (std::int64_t& x : v) x = round_div(x, div);
}

}  // namespace

Image normal_map(const Image& img, NormalOptions options) {
    Image out;
    if (img.empty() || img.rgba.size() < static_cast<std::size_t>(img.width) * img.height * 4) return out;
    const int w = img.width, h = img.height;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    const int r = std::clamp(options.smooth, 0, 64);
    std::vector<std::uint8_t> inside(n), outside(n);
    for (std::size_t i = 0; i < n; ++i) {
        inside[i] = img.rgba[i * 4 + 3] >= std::max<std::uint8_t>(options.alpha, 1) ? 1 : 0;
        outside[i] = inside[i] ? 0 : 1;
    }

    // The signed distance to the edge, in 1/256 pixel (the edge half a pixel
    // beyond the last pixel inside): positive inside, negative outside.
    const std::vector<std::int64_t> in_d2 = squared_distance(outside, w, h, true);
    const std::vector<std::int64_t> out_d2 = squared_distance(inside, w, h, false);
    std::vector<std::int64_t> sd(n);
    for (std::size_t i = 0; i < n; ++i)
        sd[i] = inside[i] ? static_cast<std::int64_t>(isqrt(static_cast<std::uint64_t>(in_d2[i]) << 16)) - 128
                          : 128 - static_cast<std::int64_t>(isqrt(static_cast<std::uint64_t>(out_d2[i]) << 16));
    blur(sd, w, h, r);

    // The height, in 1/256 pixel: a quarter circle `bevel` wide rising from the edge, level beyond it.
    const std::int64_t bevel = std::max<std::int64_t>(fixed(options.bevel, 256), 1);
    std::vector<std::int64_t> height(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::int64_t d = std::clamp<std::int64_t>(sd[i], 0, bevel);
        const std::int64_t t = bevel - d;
        height[i] = static_cast<std::int64_t>(isqrt(static_cast<std::uint64_t>(bevel * bevel - t * t)));
    }

    // Painted folds: darker is deeper. The luminance of the straight colour
    // (Rec. 601) inside, blurred with the inside's weight alone (so the
    // edge is not a fold), added in proportion to the bevel's height.
    const std::int64_t detail = fixed(options.detail, 256);
    if (detail != 0) {
        std::vector<std::int64_t> lum(n), weight(n);
        for (std::size_t i = 0; i < n; ++i) {
            if (!inside[i]) continue;
            const std::uint8_t* p = img.rgba.data() + i * 4;
            std::int64_t cr = p[0], cg = p[1], cb = p[2];
            if (img.alpha == Alpha::premultiplied && p[3] < 255) {
                cr = std::min<std::int64_t>(255, round_div(cr * 255, p[3]));
                cg = std::min<std::int64_t>(255, round_div(cg * 255, p[3]));
                cb = std::min<std::int64_t>(255, round_div(cb * 255, p[3]));
            }
            lum[i] = (77 * cr + 150 * cg + 29 * cb + 128) >> 8;
            weight[i] = 1;
        }
        if (r > 0) {
            std::vector<std::int64_t> tmp;
            for (auto* v : {&lum, &weight})
                for (int pass = 0; pass < 2; ++pass) {
                    box_sum(*v, tmp, w, h, r, true);
                    box_sum(*v, tmp, w, h, r, false);
                }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (weight[i] == 0 || height[i] == 0) continue;
            // detail · (lum / 255) · (height / bevel), in 1/256 pixel
            height[i] += round_div(detail * lum[i] * height[i], weight[i] * 255 * bevel);
        }
    }

    // Slopes by Sobel (8 × the slope, in 1/256 pixel per pixel), scaled by
    // `strength`; the normal (−∂h/∂x, −∂h/∂y, 1), made unit, as bytes.
    const std::int64_t strength = fixed(options.strength, 256);
    auto H = [&](int x, int y) { return height[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
    out.width = w;
    out.height = h;
    out.alpha = Alpha::straight;
    out.rgba.resize(n * 4);
    constexpr std::int64_t z = 8 * 256;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::int64_t sx = (H(x + 1, y - 1) + 2 * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x - 1, y) + H(x - 1, y + 1));
            const std::int64_t sy = (H(x - 1, y + 1) + 2 * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2 * H(x, y - 1) + H(x + 1, y - 1));
            const std::int64_t gx = -round_div(sx * strength, 256), gy = -round_div(sy * strength, 256);
            const std::int64_t len = std::max<std::int64_t>(1, static_cast<std::int64_t>(isqrt(static_cast<std::uint64_t>(gx * gx + gy * gy + z * z))));
            std::uint8_t* o = out.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            o[0] = static_cast<std::uint8_t>(std::clamp<std::int64_t>(128 + round_div(127 * gx, len), 1, 255));
            o[1] = static_cast<std::uint8_t>(std::clamp<std::int64_t>(128 + round_div(127 * gy, len), 1, 255));
            o[2] = static_cast<std::uint8_t>(std::clamp<std::int64_t>(128 + round_div(127 * z, len), 1, 255));
            o[3] = 255;
        }
    return out;
}

}  // namespace omui::imageio
