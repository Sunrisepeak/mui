// The built-in pixel effects' CPU reference implementations. Every one is a
// pure function of its inputs: integer hashing for randomness, anim's
// deterministic sine and cosine, no fused multiply-add — the same bytes on
// every platform and compiler (spec R-2).
module openmui.render.vector;

import std;
import openmui.math;
import openmui.geom;
import openmui.raster;
import hanim;
import :display;
import :effects;
import :cpu;

namespace omui::render {

namespace {

using raster::Rgba8;
using raster::Surface;

std::uint64_t mix(std::uint64_t x) noexcept {
    return math::mix64(x);
}
// A number in [0, 1) for (seed, x, y, salt), the same everywhere.
float hash01(float seed, std::int64_t x, std::int64_t y, std::uint64_t salt = 0) noexcept {
    const auto s = static_cast<std::uint64_t>(static_cast<std::int64_t>(seed * 4096.0f));
    const std::uint64_t h = mix(s ^ mix(static_cast<std::uint64_t>(x) * 0x9e3779b1ull ^ mix(static_cast<std::uint64_t>(y) + salt * 0x632be59bd9b4e019ull)));
    return static_cast<float>(static_cast<double>(h >> 40) * (1.0 / 16777216.0));
}

float clamp01(float v) noexcept { return v < 0 ? 0 : v > 1 ? 1 : v; }

std::uint8_t q8(float v) noexcept { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f); }

Rgba8 scaled(Rgba8 p, float k) noexcept {
    if (k >= 1) return p;
    if (k <= 0) return {};
    return {q8(p.r * k), q8(p.g * k), q8(p.b * k), q8(p.a * k)};
}

// Bilinear sample of premultiplied pixels; outside the surface is transparent.
std::array<float, 4> sample(const Surface& s, float x, float y) noexcept {
    x -= 0.5f;
    y -= 0.5f;
    const float fx = std::floor(x), fy = std::floor(y);
    const auto ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = x - fx, ty = y - fy;
    std::array<float, 4> out{};
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int px = ix + dx, py = iy + dy;
            if (px < 0 || py < 0 || px >= s.width() || py >= s.height()) continue;
            const float w = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty);
            const Rgba8 p = s.at(px, py);
            out[0] += p.r * w;
            out[1] += p.g * w;
            out[2] += p.b * w;
            out[3] += p.a * w;
        }
    return out;
}

// A visibility ramp: 1 for a value well past the threshold, 0 before it,
// `soft` wide; `progress` 0 shows everything, 1 nothing.
float ramp(float value, float progress, float soft) noexcept {
    soft = std::max(soft, 1e-4f);
    return clamp01((value - (progress * (1 + soft) - soft)) / soft);
}

template <class F>
void each_pixel(const EffectInput& in, F f) {
    for (int y = in.row_begin; y < in.last_row(); ++y) {
        Rgba8* out = in.target.row(y);
        const Rgba8* src = in.source.row(y);
        for (int x = 0; x < in.target.width(); ++x) out[x] = f(x, y, src[x]);
    }
}

// ── the effects ──

float dissolve_cell(std::span<const float> params, float scale) { return std::max(1.0f, std::round(params[3] * scale)); }

void dissolve(const EffectInput& in) {
    const float p = in.params[0], seed = in.params[1], soft = in.params[2];
    const float cell = dissolve_cell(in.params, in.scale);
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        if (s.a == 0) return s;
        const float n = hash01(seed, static_cast<std::int64_t>(std::floor(static_cast<float>(x) / cell)),
                               static_cast<std::int64_t>(std::floor(static_cast<float>(y) / cell)));
        return scaled(s, ramp(n, p, soft));
    });
}

// The direction's cosine and sine, and the layer's extent along it.
struct WipeFrame {
    float c, sn, lo, span;
};
WipeFrame wipe_frame(std::span<const float> params, geom::Rect r) {
    const double a = static_cast<double>(params[1]) * hanim::dm::pi / 180.0;
    const auto c = static_cast<float>(hanim::dm::cos(a)), sn = static_cast<float>(hanim::dm::sin(a));
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (geom::Point q : {geom::Point{r.x, r.y}, {r.right(), r.y}, {r.x, r.bottom()}, {r.right(), r.bottom()}}) {
        lo = std::min(lo, q.x * c + q.y * sn);
        hi = std::max(hi, q.x * c + q.y * sn);
    }
    return {c, sn, lo, std::max(hi - lo, 1.0f)};
}

void wipe(const EffectInput& in) {
    const float p = in.params[0], soft = in.params[2];
    const auto [c, sn, lo, span] = wipe_frame(in.params, in.layer);
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        if (s.a == 0) return s;
        const float t = ((static_cast<float>(x) + 0.5f) * c + (static_cast<float>(y) + 0.5f) * sn - lo) / span;
        return scaled(s, ramp(t, p, soft));
    });
}

// The circle's centre and the distance to the layer's farthest corner.
struct RevealFrame {
    float cx, cy, far;
};
RevealFrame reveal_frame(std::span<const float> params, geom::Rect r) {
    const float cx = r.x + r.width * params[1], cy = r.y + r.height * params[2];
    float far = 0;
    for (geom::Point q : {geom::Point{r.x, r.y}, {r.right(), r.y}, {r.x, r.bottom()}, {r.right(), r.bottom()}})
        far = std::max(far, std::sqrt((q.x - cx) * (q.x - cx) + (q.y - cy) * (q.y - cy)));
    return {cx, cy, std::max(far, 1.0f)};
}

void reveal(const EffectInput& in) {
    const float p = in.params[0], soft = in.params[3];
    const auto [cx, cy, far] = reveal_frame(in.params, in.layer);
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        if (s.a == 0) return s;
        const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
        const float d = std::sqrt(dx * dx + dy * dy) / far;
        // Inside the growing circle: shown, `soft` wide at its edge.
        return scaled(s, clamp01((p * (1 + soft) - d) / std::max(soft, 1e-4f)));
    });
}

// Row y's shift.
float displace_row(std::span<const float> params, float scale, int y) {
    const float amount = params[0] * scale, wavelength = std::max(1.0f, params[1] * scale), phase = params[2];
    const double arg = 2 * hanim::dm::pi * ((static_cast<double>(y) + 0.5) / wavelength + phase);
    return amount * static_cast<float>(hanim::dm::sin(arg));
}

void displace(const EffectInput& in) {
    for (int y = in.row_begin; y < in.last_row(); ++y) {
        const float dx = displace_row(in.params, in.scale, y);
        Rgba8* out = in.target.row(y);
        for (int x = 0; x < in.target.width(); ++x) {
            const auto v = sample(in.source, static_cast<float>(x) + 0.5f - dx, static_cast<float>(y) + 0.5f);
            out[x] = {q8(v[0]), q8(v[1]), q8(v[2]), q8(v[3])};
        }
    }
}

void noise(const EffectInput& in) {
    const float amount = in.params[0], seed = in.params[1];
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        if (s.a == 0) return s;
        const float k = (hash01(seed, x, y) - 0.5f) * 2 * amount * s.a;
        auto ch = [&](std::uint8_t c) { return q8(std::clamp(c + k, 0.0f, static_cast<float>(s.a))); };
        return Rgba8{ch(s.r), ch(s.g), ch(s.b), s.a};
    });
}

geom::Point chromatic_offset(std::span<const float> params, float scale) {
    const double a = static_cast<double>(params[1]) * hanim::dm::pi / 180.0;
    return {params[0] * scale * static_cast<float>(hanim::dm::cos(a)), params[0] * scale * static_cast<float>(hanim::dm::sin(a))};
}

void chromatic(const EffectInput& in) {
    const auto [ox, oy] = chromatic_offset(in.params, in.scale);
    for (int y = in.row_begin; y < in.last_row(); ++y) {
        Rgba8* out = in.target.row(y);
        for (int x = 0; x < in.target.width(); ++x) {
            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
            const auto r = sample(in.source, px - ox, py - oy);
            const auto g = sample(in.source, px, py);
            const auto b = sample(in.source, px + ox, py + oy);
            const float alpha = std::max({r[3], g[3], b[3]});
            out[x] = {q8(std::min(r[0], alpha)), q8(std::min(g[1], alpha)), q8(std::min(b[2], alpha)), q8(alpha)};
        }
    }
}

// Box blur of radius r along one axis, in integers (sums of premultiplied
// bytes); lines [first, last) of it (rows or columns, each on its own).
void box_pass(const Surface& from, Surface& to, int r, bool horizontal, int first = 0, int last = -1) {
    const int w = from.width(), h = from.height();
    const int n = 2 * r + 1;
    const int lines = horizontal ? h : w, length = horizontal ? w : h;
    if (last < 0 || last > lines) last = lines;
    for (int line = first; line < last; ++line) {
        auto px = [&](int i) -> const Rgba8& {
            return horizontal ? from.at(i, line) : from.at(line, i);
        };
        std::array<std::int64_t, 4> sum{};
        for (int i = -r; i <= r; ++i)
            if (i >= 0 && i < length) {
                const Rgba8& p = px(i);
                sum[0] += p.r, sum[1] += p.g, sum[2] += p.b, sum[3] += p.a;
            }
        for (int i = 0; i < length; ++i) {
            Rgba8& o = horizontal ? to.at(i, line) : to.at(line, i);
            o = {static_cast<std::uint8_t>((sum[0] + n / 2) / n), static_cast<std::uint8_t>((sum[1] + n / 2) / n),
                 static_cast<std::uint8_t>((sum[2] + n / 2) / n), static_cast<std::uint8_t>((sum[3] + n / 2) / n)};
            const int out_i = i - r, in_i = i + r + 1;
            if (out_i >= 0) {
                const Rgba8& p = px(out_i);
                sum[0] -= p.r, sum[1] -= p.g, sum[2] -= p.b, sum[3] -= p.a;
            }
            if (in_i < length) {
                const Rgba8& p = px(in_i);
                sum[0] += p.r, sum[1] += p.g, sum[2] += p.b, sum[3] += p.a;
            }
        }
    }
}

int blur_radius(std::span<const float> params, float scale) { return box_radius(params[0], scale); }

void blur(const EffectInput& in) {
    const int r = blur_radius(in.params, in.scale);
    if (r <= 0) {
        each_pixel(in, [](int, int, Rgba8 s) { return s; });
        return;
    }
    // Three box passes each way approach a Gaussian of the given radius.
    Surface a = in.source;
    box_blur(a, r);
    each_pixel(in, [&](int x, int y, Rgba8) { return a.at(x, y); });
}

int pixelate_size(std::span<const float> params, float scale) { return std::max(1, static_cast<int>(std::round(params[0] * scale))); }

void pixelate(const EffectInput& in) {
    const int size = pixelate_size(in.params, in.scale);
    const int ox = static_cast<int>(std::floor(in.layer.x)), oy = static_cast<int>(std::floor(in.layer.y));
    auto first = [&](int v, int o) { return o + static_cast<int>(std::floor(static_cast<float>(v - o) / static_cast<float>(size))) * size; };
    for (int by = first(0, oy); by < in.target.height(); by += size)
        for (int bx = first(0, ox); bx < in.target.width(); bx += size) {
            std::array<std::int64_t, 4> sum{};
            std::int64_t count = 0;
            for (int y = std::max(by, 0); y < std::min(by + size, in.target.height()); ++y)
                for (int x = std::max(bx, 0); x < std::min(bx + size, in.target.width()); ++x) {
                    const Rgba8 p = in.source.at(x, y);
                    sum[0] += p.r, sum[1] += p.g, sum[2] += p.b, sum[3] += p.a;
                    ++count;
                }
            if (count == 0) continue;
            const Rgba8 avg{static_cast<std::uint8_t>((sum[0] + count / 2) / count), static_cast<std::uint8_t>((sum[1] + count / 2) / count),
                            static_cast<std::uint8_t>((sum[2] + count / 2) / count), static_cast<std::uint8_t>((sum[3] + count / 2) / count)};
            for (int y = std::max(by, 0); y < std::min(by + size, in.target.height()); ++y)
                for (int x = std::max(bx, 0); x < std::min(bx + size, in.target.width()); ++x) in.target.at(x, y) = avg;
        }
}

Rgba8 over(Rgba8 src, Rgba8 dst) noexcept {
    if (src.a == 255 || dst.a == 0) return src;
    if (src.a == 0) return dst;
    const int k = 255 - src.a;
    auto ch = [&](int s, int d) { return static_cast<std::uint8_t>(std::min(255, s + (d * k + 127) / 255)); };
    return {ch(src.r, dst.r), ch(src.g, dst.g), ch(src.b, dst.b), ch(src.a, dst.a)};
}

// The layer's pixels as square particles: each starts after a seeded delay,
// flies in a seeded direction `spread` far while falling `gravity`, and fades.
// Progress 0 is the layer as it is; 1, every particle gone. Run backwards it
// gathers the layer out of a cloud.
struct ParticlesFrame {
    float spread, gravity;
    int   size;
    float stagger;
    int   x0, y0, x1, y1;
};
ParticlesFrame particles_frame(std::span<const float> params, float scale, geom::Rect layer) {
    return {params[2] * scale,
            params[3] * scale,
            std::max(1, static_cast<int>(std::round(params[4] * scale))),
            std::clamp(params[5], 0.0f, 0.95f),
            static_cast<int>(std::floor(layer.x)),
            static_cast<int>(std::floor(layer.y)),
            static_cast<int>(std::ceil(layer.right())),
            static_cast<int>(std::ceil(layer.bottom()))};
}

void particles(const EffectInput& in) {
    const float p = in.params[0], seed = in.params[1];
    const auto [spread, gravity, size, stagger, x0, y0, x1, y1] = particles_frame(in.params, in.scale, in.layer);
    for (int cy = y0; cy < y1; cy += size)
        for (int cx = x0; cx < x1; cx += size) {
            const std::int64_t i = (cx - x0) / size, j = (cy - y0) / size;
            const float delay = hash01(seed, i, j, 1) * stagger;
            const float q = clamp01((p - delay) / (1 - stagger));
            if (q >= 1) continue;
            const float e = q * (2 - q);  // out-quad: a burst that slows
            const float dx = (hash01(seed, i, j, 2) * 2 - 1) * spread * e;
            const float dy = (hash01(seed, i, j, 3) * 2 - 1) * spread * e + gravity * q * q;
            const float alpha = 1 - q;
            const int mx = static_cast<int>(std::lround(dx)), my = static_cast<int>(std::lround(dy));
            for (int y = cy; y < std::min(cy + size, y1); ++y)
                for (int x = cx; x < std::min(cx + size, x1); ++x) {
                    if (x < 0 || y < 0 || x >= in.source.width() || y >= in.source.height()) continue;
                    const Rgba8 s = in.source.at(x, y);
                    if (s.a == 0) continue;
                    const int tx = x + mx, ty = y + my;
                    if (tx < 0 || ty < 0 || tx >= in.target.width() || ty >= in.target.height()) continue;
                    Rgba8& d = in.target.at(tx, ty);
                    d = over(scaled(s, alpha), d);
                }
        }
}

// ── liquid glass (D-64) ──
//
// A thick transparent material over what lies under it: the backdrop bent
// at the shape's bevel (refraction from the signed distance's gradient),
// split a little by colour (dispersion), frosted (the renderer's box blur),
// tinted — darker over a bright backdrop, lighter over a dark one, so what is
// on the glass stays legible — lit at the rim facing the light and along a
// fine inner edge; the layer's own content over it. Every operation is the
// GPU path's (effects/liquid_glass.slang), in the same order.

struct Glass {
    float thickness, ior, dispersion;
    float tint[4];
    float lx, ly;  // the light's direction, unit
    float smoothness;
    bool  adaptive;
    float rim;
};

Glass glass_of(std::span<const float> p, float scale) {
    Glass g{};
    g.thickness = std::max(p[0] * scale, 1.0f);
    g.ior = p[1];
    g.dispersion = p[3];
    for (int i = 0; i < 4; ++i) g.tint[i] = clamp01(p[static_cast<std::size_t>(4 + i)]);
    const float ll = std::sqrt(p[8] * p[8] + p[9] * p[9]);
    g.lx = ll > 0 ? p[8] / ll : 0.0f;
    g.ly = ll > 0 ? p[9] / ll : 0.0f;
    g.smoothness = p[10] * scale;
    g.adaptive = p[11] >= 0.5f;
    g.rim = clamp01(p[12]);
    return g;
}

float shapes_distance(std::span<const EffectShape> shapes, float x, float y, float k) {
    float d = shape_distance(shapes[0], {x, y});
    for (std::size_t i = 1; i < shapes.size() && i < 8; ++i) d = smooth_min(d, shape_distance(shapes[i], {x, y}), k);
    return d;
}

void liquid_glass(const EffectInput& in) {
    const Glass g = glass_of(in.params, in.scale);
    const Surface empty;
    const Surface& under = in.backdrop ? *in.backdrop : empty;
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        const float qx = static_cast<float>(x) + 0.5f, qy = static_cast<float>(y) + 0.5f;
        const float d = shapes_distance(in.shapes, qx, qy, g.smoothness);
        const float cov = clamp01(0.5f - d);
        if (cov <= 0) return s;
        // The bevel: 0 at the edge, 1 where the glass is flat.
        const float e = std::max(-d, 0.0f);
        const float t = clamp01(e / g.thickness);
        // The surface's normal, outward: the distance's gradient.
        const float gx = (shapes_distance(in.shapes, qx + 1, qy, g.smoothness) - shapes_distance(in.shapes, qx - 1, qy, g.smoothness)) * 0.5f;
        const float gy = (shapes_distance(in.shapes, qx, qy + 1, g.smoothness) - shapes_distance(in.shapes, qx, qy - 1, g.smoothness)) * 0.5f;
        const float gl = std::sqrt(gx * gx + gy * gy);
        const float nx = gl > 0 ? gx / gl : 0.0f, ny = gl > 0 ? gy / gl : 0.0f;
        // Refraction: the backdrop pulled in from beyond the edge.
        const float bend = (1 - t) * (1 - t);
        const float off = (g.ior - 1) * g.thickness * bend;
        const float spread = g.dispersion * 0.5f;
        const float ro = off * (1 + spread), bo = off * (1 - spread);
        const auto red = sample(under, qx - nx * ro, qy - ny * ro);
        const auto green = sample(under, qx - nx * off, qy - ny * off);
        const auto blue = sample(under, qx - nx * bo, qy - ny * bo);
        float c[4] = {std::min(red[0], green[3]), green[1], std::min(blue[2], green[3]), green[3]};
        // Tint, over (premultiplied).
        const float ta = g.tint[3];
        for (int i = 0; i < 3; ++i) c[i] = g.tint[i] * ta * 255.0f + c[i] * (1 - ta);
        c[3] = ta * 255.0f + c[3] * (1 - ta);
        // Adaptive: what is on the glass stays legible.
        if (g.adaptive && c[3] > 0) {
            const float lum = (0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]) / c[3];
            const float darken = 0.08f * clamp01((lum - 0.55f) / 0.35f), lighten = 0.08f * clamp01((0.45f - lum) / 0.35f);
            for (int i = 0; i < 3; ++i) c[i] = c[i] * (1 - darken) + c[3] * lighten;
        }
        // Light: the rim facing it, and a fine line inside the edge.
        const float facing = std::max(0.0f, -(nx * g.lx + ny * g.ly));
        const float rim = g.rim * (1 - t) * (1 - t) * (1 - t) * facing;
        const float line = g.rim * 0.5f * clamp01(1 - e / (1.5f * in.scale));
        const float light = std::min(rim + line, 1.0f);
        for (int i = 0; i < 3; ++i) c[i] = std::min(c[i] + (c[3] - c[i]) * light, c[3]);
        // The glass where it covers; the layer's content over it.
        const float inv = 1 - static_cast<float>(s.a) / 255.0f;
        auto ch = [&](std::uint8_t sv, float gv) { return q8(static_cast<float>(sv) + gv * cov * inv); };
        return Rgba8{ch(s.r, c[0]), ch(s.g, c[1]), ch(s.b, c[2]), ch(s.a, c[3])};
    });
}

// ── the style packs' effects (S2): neon, crt, paper, wobble, metaball ──

// neon: light around the layer's shapes — a glow outside fading over
// `radius`, a bright tube just inside the edge — under the content, and a
// band of light sweeping across it.
void neon(const EffectInput& in) {
    const float cr = clamp01(in.params[0]), cg = clamp01(in.params[1]), cb = clamp01(in.params[2]);
    const float radius = std::max(in.params[3] * in.scale, 1.0f), intensity = std::clamp(in.params[4], 0.0f, 2.0f);
    const float sweep = in.params[5], width = std::max(in.params[6] * in.scale, 1.0f);
    const float bx = in.layer.x + in.layer.width * sweep;
    const float tube = 1.5f * in.scale;
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        const float qx = static_cast<float>(x) + 0.5f, qy = static_cast<float>(y) + 0.5f;
        const float d = shapes_distance(in.shapes, qx, qy, 0);
        float g = 0;
        if (d > 0) {
            const float t = 1 - d / radius;
            if (t > 0) g = intensity * t * t;
        } else {
            g = intensity * clamp01(1 + d / tube);
        }
        g = clamp01(g);
        float c[4] = {static_cast<float>(s.r), static_cast<float>(s.g), static_cast<float>(s.b), static_cast<float>(s.a)};
        if (sweep >= 0 && d <= 0 && s.a > 0) {
            const float f = clamp01(1 - std::abs(qx - bx) / width) * 0.6f;
            for (int i = 0; i < 3; ++i) c[i] = c[i] + (c[3] - c[i]) * f;
        }
        const float inv = 1 - c[3] / 255.0f;
        const float a = 255.0f * g * inv;
        return Rgba8{q8(c[0] + cr * a), q8(c[1] + cg * a), q8(c[2] + cb * a), q8(c[3] + a)};
    });
}

// crt: an old screen over the layer — bent outward (barrel), lit in
// scanlines (one factor a row, the aux texture's on the GPU), seen through an
// aperture grille, darker towards its corners; black beyond the glass.
float crt_row(std::span<const float> params, float scale, int y) {
    const float strength = clamp01(params[1]);
    const double period = std::max(2.0, 3.0 * static_cast<double>(scale));
    const double phase = (static_cast<double>(y) + 0.5) / period;
    const double wave = 0.5 - 0.5 * hanim::dm::cos(2 * hanim::dm::pi * phase);
    return static_cast<float>(1.0 - static_cast<double>(strength) * 0.6 * wave);
}
void rows_crt(std::span<const float> p, float scale, std::span<float> out) {
    for (std::size_t y = 0; y < out.size(); ++y) out[y] = crt_row(p, scale, static_cast<int>(y));
}
void crt(const EffectInput& in) {
    const float curve = std::clamp(in.params[0], 0.0f, 0.5f), mask = clamp01(in.params[2]), vignette = clamp01(in.params[3]);
    const float hw = std::max(in.layer.width * 0.5f, 1.0f), hh = std::max(in.layer.height * 0.5f, 1.0f);
    const float cx = in.layer.x + hw, cy = in.layer.y + hh;
    for (int y = in.row_begin; y < in.last_row(); ++y) {
        const float scan = crt_row(in.params, in.scale, y);
        Rgba8* out = in.target.row(y);
        for (int x = 0; x < in.target.width(); ++x) {
            const float qx = static_cast<float>(x) + 0.5f, qy = static_cast<float>(y) + 0.5f;
            const float u = (qx - cx) / hw, v = (qy - cy) / hh;
            const float r2 = u * u + v * v;
            const float f = 1 + curve * r2;
            const float su = u * f, sv = v * f;
            if (std::abs(su) > 1 || std::abs(sv) > 1) {
                out[x] = Rgba8{0, 0, 0, 255};
                continue;
            }
            const auto c = sample(in.source, cx + su * hw, cy + sv * hh);
            const int col = x % 3;
            const float mr = col == 0 ? 1 : 1 - mask * 0.5f, mg = col == 1 ? 1 : 1 - mask * 0.5f, mb = col == 2 ? 1 : 1 - mask * 0.5f;
            const float vig = 1 - vignette * 0.5f * r2;
            const float k = scan * std::max(vig, 0.0f);
            out[x] = Rgba8{q8(c[0] * mr * k), q8(c[1] * mg * k), q8(c[2] * mb * k), q8(c[3])};
        }
    }
}

// paper: what is drawn as if printed on paper — a warm tone, a fine grain
// (seeded, `grain_size` logical pixels), darker towards the edges.
void paper(const EffectInput& in) {
    const float grain = clamp01(in.params[0]), warmth = clamp01(in.params[1]), seed = in.params[2];
    const float size = std::max(1.0f, std::round(in.params[3] * in.scale)), edge = clamp01(in.params[4]);
    const float hw = std::max(in.layer.width * 0.5f, 1.0f), hh = std::max(in.layer.height * 0.5f, 1.0f);
    const float cx = in.layer.x + hw, cy = in.layer.y + hh;
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        if (s.a == 0) return s;
        const float n = hash01(seed, static_cast<std::int64_t>(std::floor(static_cast<float>(x) / size)),
                               static_cast<std::int64_t>(std::floor(static_cast<float>(y) / size)), 3);
        const float a = static_cast<float>(s.a);
        const float k = (n - 0.5f) * 2 * grain * 0.12f * a;
        const float u = (static_cast<float>(x) + 0.5f - cx) / hw, v = (static_cast<float>(y) + 0.5f - cy) / hh;
        const float dark = 1 - edge * 0.25f * (u * u + v * v);
        auto ch = [&](std::uint8_t c, float tone) { return q8(std::clamp((static_cast<float>(c) * tone + k) * dark, 0.0f, a)); };
        return Rgba8{ch(s.r, 1), ch(s.g, 1 - warmth * 0.04f), ch(s.b, 1 - warmth * 0.12f), s.a};
    });
}

// tone: the layer in one colour — each pixel's brightness (Rec. 709 luma)
// kept, its hue the tint's (the tint's brightest channel at full), mixed by
// `amount`: a world gone grey while time stands still, a memory, a page
// that is not in use.
void tone(const EffectInput& in) {
    const float tr = clamp01(in.params[0]), tg = clamp01(in.params[1]), tb = clamp01(in.params[2]), amount = clamp01(in.params[3]);
    const float top = std::max({tr, tg, tb, 0.001f});
    const float kr = tr / top, kg = tg / top, kb = tb / top;
    each_pixel(in, [&](int, int, Rgba8 s) {
        if (s.a == 0) return s;
        const float a = static_cast<float>(s.a);
        const float y = 0.2126f * static_cast<float>(s.r) + 0.7152f * static_cast<float>(s.g) + 0.0722f * static_cast<float>(s.b);
        auto ch = [&](std::uint8_t c, float k) {
            const float v = static_cast<float>(c);
            return q8(v + (std::min(y * k, a) - v) * amount);
        };
        return Rgba8{ch(s.r, kr), ch(s.g, kg), ch(s.b, kb), s.a};
    });
}

// wobble: lines as if drawn by hand — the layer pushed about by a smooth
// noise (`amount` logical pixels, features `size` wide; a new `seed`, a new
// drawing: animate it in steps for a hand-drawn boil), inked at its edges.
float smooth_noise(float seed, float x, float y, std::uint64_t salt) {
    const float fx = std::floor(x), fy = std::floor(y);
    const auto ix = static_cast<std::int64_t>(fx), iy = static_cast<std::int64_t>(fy);
    const float tx = x - fx, ty = y - fy;
    const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = hash01(seed, ix, iy, salt), b = hash01(seed, ix + 1, iy, salt);
    const float c = hash01(seed, ix, iy + 1, salt), d = hash01(seed, ix + 1, iy + 1, salt);
    const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
    return (top + (bottom - top) * sy) * 2 - 1;
}
void wobble(const EffectInput& in) {
    const float amount = in.params[0] * in.scale, size = std::max(1.0f, in.params[1] * in.scale), seed = in.params[2], ink = clamp01(in.params[3]);
    for (int y = in.row_begin; y < in.last_row(); ++y) {
        Rgba8* out = in.target.row(y);
        for (int x = 0; x < in.target.width(); ++x) {
            const float qx = static_cast<float>(x) + 0.5f, qy = static_cast<float>(y) + 0.5f;
            const float ox = smooth_noise(seed, qx / size, qy / size, 1) * amount, oy = smooth_noise(seed, qx / size, qy / size, 2) * amount;
            const auto c = sample(in.source, qx - ox, qy - oy);
            // Ink where the alpha changes: an edge.
            const auto l = sample(in.source, qx - ox - 1, qy - oy), r = sample(in.source, qx - ox + 1, qy - oy);
            const auto u = sample(in.source, qx - ox, qy - oy - 1), d = sample(in.source, qx - ox, qy - oy + 1);
            const float e = clamp01((std::abs(r[3] - l[3]) + std::abs(d[3] - u[3])) / 255.0f);
            const float k = 1 - ink * 0.7f * e;
            out[x] = Rgba8{q8(c[0] * k), q8(c[1] * k), q8(c[2] * k), q8(c[3])};
        }
    }
}

// metaball: the layer's shapes merge like drops (`smoothness` logical
// pixels apart); where they bridge, the bridge is filled with `colour`, and
// the layer's content lies over it — switches and sliders that flow.
void metaball(const EffectInput& in) {
    const float k = in.params[0] * in.scale;
    const float cr = clamp01(in.params[1]), cg = clamp01(in.params[2]), cb = clamp01(in.params[3]), ca = clamp01(in.params[4]);
    each_pixel(in, [&](int x, int y, Rgba8 s) {
        const float qx = static_cast<float>(x) + 0.5f, qy = static_cast<float>(y) + 0.5f;
        const float d = shapes_distance(in.shapes, qx, qy, k);
        const float cov = clamp01(0.5f - d);
        if (cov <= 0) return s;
        const float inv = 1 - static_cast<float>(s.a) / 255.0f;
        const float a = 255.0f * ca * cov * inv;
        return Rgba8{q8(static_cast<float>(s.r) + cr * a), q8(static_cast<float>(s.g) + cg * a), q8(static_cast<float>(s.b) + cb * a),
                     q8(static_cast<float>(s.a) + a)};
    });
}

void derive_glass(std::span<const float>, float, geom::Rect, EffectFrame&) {}

float no_reach(std::span<const float>) { return 0; }

// ── the GPU paths' derived values: the CPU reference's own computations ──

namespace fx = shaders;  // effects/*.slang

void derive_dissolve(std::span<const float> p, float scale, geom::Rect, EffectFrame& out) { out.derived[0] = dissolve_cell(p, scale); }
void derive_wipe(std::span<const float> p, float, geom::Rect layer, EffectFrame& out) {
    const WipeFrame w = wipe_frame(p, layer);
    out.derived = {w.c, w.sn, w.lo, w.span, 0, 0, 0, 0};
}
void derive_reveal(std::span<const float> p, float, geom::Rect layer, EffectFrame& out) {
    const RevealFrame r = reveal_frame(p, layer);
    out.derived = {r.cx, r.cy, r.far, 0, 0, 0, 0, 0};
}
void rows_displace(std::span<const float> p, float scale, std::span<float> out) {
    for (std::size_t y = 0; y < out.size(); ++y) out[y] = displace_row(p, scale, static_cast<int>(y));
}
void derive_chromatic(std::span<const float> p, float scale, geom::Rect, EffectFrame& out) {
    const geom::Point o = chromatic_offset(p, scale);
    out.derived[0] = o.x;
    out.derived[1] = o.y;
}
void derive_blur(std::span<const float> p, float scale, geom::Rect, EffectFrame& out) { out.derived[0] = static_cast<float>(blur_radius(p, scale)); }
void derive_pixelate(std::span<const float> p, float scale, geom::Rect layer, EffectFrame& out) {
    out.derived[0] = static_cast<float>(pixelate_size(p, scale));
    out.derived[1] = std::floor(layer.x);
    out.derived[2] = std::floor(layer.y);
}
void derive_particles(std::span<const float> p, float scale, geom::Rect layer, EffectFrame& out) {
    const ParticlesFrame f = particles_frame(p, scale, layer);
    out.derived = {static_cast<float>(f.size), static_cast<float>(f.x0), static_cast<float>(f.y0), static_cast<float>(f.x1),
                   static_cast<float>(f.y1),   f.stagger,                  f.spread,                   f.gravity};
}

EffectGpu pixel(const rhi::ShaderDesc& fs, decltype(EffectGpu::derive) derive = nullptr, decltype(EffectGpu::rows) rows = nullptr) {
    return {EffectPass::pixel, &fs, derive, rows};
}

// An effect whose every row depends on its inputs alone.
EffectInfo local(EffectInfo info) {
    info.row_local = true;
    return info;
}

void register_builtins(EffectRegistry& r) {
    r.add(local({"dissolve", "Pixels (or cells of `grain` pixels) vanish in a seeded random order as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: all shown; 1: all gone"},
            {"seed", 1, 0, 1e6f, "which random order"},
            {"softness", 0.15f, 0.001f, 1, "how gradually each pixel fades"},
            {"grain", 1, 1, 64, "cell size in logical pixels"}},
           no_reach, dissolve, pixel(fx::dissolve::dissolve_fs, derive_dissolve)}));
    r.add(local({"wipe", "The layer is wiped away from one edge as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: all shown; 1: all gone"},
            {"angle", 0, -360, 360, "direction of travel in degrees (0: left to right)"},
            {"softness", 0.1f, 0.001f, 1, "width of the edge"}},
           no_reach, wipe, pixel(fx::wipe::wipe_fs, derive_wipe)}));
    r.add(local({"reveal", "A circle grows from a point and shows the layer as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: hidden; 1: all shown"},
            {"x", 0.5f, 0, 1, "centre, as a fraction of the layer's width"},
            {"y", 0.5f, 0, 1, "centre, as a fraction of the layer's height"},
            {"softness", 0.08f, 0.001f, 1, "width of the edge"}},
           no_reach, reveal, pixel(fx::reveal::reveal_fs, derive_reveal)}));
    r.add(local({"displace", "Rows shift sideways along a sine wave (heat haze, water, a flag).",
           {{"amount", 4, 0, 200, "largest shift in logical pixels"},
            {"wavelength", 40, 1, 2000, "wave length in logical pixels"},
            {"phase", 0, 0, 1, "position of the wave (animate it to make it travel)"}},
           [](std::span<const float> p) { return std::abs(p[0]); }, displace, pixel(fx::displace::displace_fs, nullptr, rows_displace)}));
    r.add(local({"noise", "Seeded grain over the colours.",
           {{"amount", 0.15f, 0, 1, "strength"}, {"seed", 1, 0, 1e6f, "which grain"}}, no_reach, noise, pixel(fx::noise::noise_fs)}));
    r.add(local({"chromatic", "Red and blue drift apart from green, as through a cheap lens.",
           {{"offset", 2, 0, 100, "distance in logical pixels"}, {"angle", 0, -360, 360, "direction in degrees"}},
           [](std::span<const float> p) { return std::abs(p[0]); }, chromatic, pixel(fx::chromatic::chromatic_fs, derive_chromatic)}));
    r.add({"blur", "A Gaussian-like blur (three box passes each way).",
           {{"radius", 4, 0, 200, "radius in logical pixels"}}, [](std::span<const float> p) { return std::abs(p[0]); }, blur,
           {EffectPass::blur, nullptr, derive_blur, nullptr}});
    r.add({"pixelate", "The layer in square blocks of their average colour.",
           {{"size", 6, 1, 256, "block size in logical pixels"}}, no_reach, pixelate, {EffectPass::pixelate, nullptr, derive_pixelate, nullptr}});
    r.add({"particles", "The layer bursts into square particles that fly, fall and fade as progress goes from 0 to 1 "
                        "(backwards: gathers out of a cloud).",
           {{"progress", 0, 0, 1, "0: whole; 1: gone"},
            {"seed", 1, 0, 1e6f, "which directions and delays"},
            {"spread", 80, 0, 2000, "how far particles fly, logical pixels"},
            {"gravity", 40, -2000, 2000, "how far they fall meanwhile"},
            {"size", 3, 1, 64, "particle size in logical pixels"},
            {"stagger", 0.5f, 0, 0.95f, "how spread out their starts are"}},
           [](std::span<const float> p) { return std::abs(p[2]) + std::abs(p[3]) + p[4]; }, particles,
           {EffectPass::particles, nullptr, derive_particles, nullptr}});
    // The style packs' (S2).
    r.add(local({"neon", "Light around the layer's shapes: a glow outside, a bright tube at the edge, a band sweeping across.",
           {{"color_r", 0.13f, 0, 1, "the light's colour, red"},
            {"color_g", 0.9f, 0, 1, "green"},
            {"color_b", 1, 0, 1, "blue"},
            {"radius", 14, 0, 200, "how far the glow reaches, logical pixels"},
            {"intensity", 0.9f, 0, 2, "how bright"},
            {"sweep", -1, -1, 2, "where the band of light is across the layer (0…1; below 0: none)"},
            {"sweep_width", 24, 1, 400, "the band's half width, logical pixels"}},
           [](std::span<const float> p) { return std::abs(p[3]); }, neon, pixel(fx::neon::neon_fs)}));
    r.add(local({"crt", "An old screen: bent outward, in scanlines, through an aperture grille, darker towards its corners.",
           {{"curvature", 0.08f, 0, 0.5f, "how much the glass bulges"},
            {"scanlines", 0.5f, 0, 1, "how dark the lines between scanlines are"},
            {"mask", 0.35f, 0, 1, "the aperture grille's strength"},
            {"vignette", 0.6f, 0, 1, "darkening towards the corners"}},
           no_reach, crt, pixel(fx::crt::crt_fs, nullptr, rows_crt)}));
    r.add(local({"paper", "As if printed on paper: a warm tone, a fine grain, darker towards the edges.",
           {{"grain", 0.35f, 0, 1, "the grain's strength"},
            {"warmth", 0.6f, 0, 1, "how warm the paper is"},
            {"seed", 7, 0, 1e6f, "which grain"},
            {"grain_size", 1, 1, 16, "the grain's size, logical pixels"},
            {"edge", 0.5f, 0, 1, "darkening towards the edges"}},
           no_reach, paper, pixel(fx::paper::paper_fs)}));
    r.add(local({"wobble", "Drawn by hand: the layer pushed about by a smooth noise and inked at its edges (a new seed, a new drawing).",
           {{"amount", 1.5f, 0, 50, "how far lines wander, logical pixels"},
            {"size", 18, 1, 400, "the wander's feature size, logical pixels"},
            {"seed", 1, 0, 1e6f, "which drawing (step it for a boil)"},
            {"ink", 0.5f, 0, 1, "how dark edges are inked"}},
           [](std::span<const float> p) { return std::abs(p[0]) + 1; }, wobble, pixel(fx::wobble::wobble_fs)}));
    r.add(local({"metaball", "The layer's shapes merge like drops; the bridges between them are filled, the content over them.",
           {{"smoothness", 16, 0, 400, "how far apart shapes still merge, logical pixels"},
            {"color_r", 0.31f, 0, 1, "the bridges' colour, red"},
            {"color_g", 0.27f, 0, 1, "green"},
            {"color_b", 0.9f, 0, 1, "blue"},
            {"color_a", 1, 0, 1, "opacity"}},
           [](std::span<const float> p) { return std::abs(p[0]); }, metaball, pixel(fx::metaball::metaball_fs)}));
    {
        EffectInfo glass{"liquid_glass",
                         "A thick transparent material over what lies under it (D-64): the backdrop bent at the bevel, split by colour, "
                         "frosted, tinted to keep its content legible, lit at the rim.",
                         {{"thickness", 12, 0, 200, "the bevel's width, logical pixels: how far in from the edge the backdrop bends"},
                          {"ior", 1.45f, 1, 3, "index of refraction: how strongly it bends"},
                          {"blur", 0, 0, 100, "frosting: the backdrop's blur radius, logical pixels"},
                          {"dispersion", 0.3f, 0, 1, "colour split at the bevel (0: none)"},
                          {"tint_r", 1, 0, 1, "tint, red"},
                          {"tint_g", 1, 0, 1, "tint, green"},
                          {"tint_b", 1, 0, 1, "tint, blue"},
                          {"tint_a", 0.1f, 0, 1, "tint, opacity"},
                          {"light_x", 0.4f, -1, 1, "the light's direction, x"},
                          {"light_y", -0.8f, -1, 1, "the light's direction, y (up is negative)"},
                          {"smoothness", 0, 0, 200, "how far apart a group's members merge like drops, logical pixels"},
                          {"adaptive", 1, 0, 1, "darker over a bright backdrop, lighter over a dark one (0: off)"},
                          {"rim", 0.6f, 0, 1, "the rim light's strength"}},
                         [](std::span<const float> p) { return std::abs(p[0]) * 2 + std::abs(p[2]) + p[10]; },
                         liquid_glass,
                         pixel(fx::liquid_glass::liquid_glass_fs, derive_glass)};
        glass.backdrop = true;
        glass.row_local = true;
        glass.backdrop_blur = [](std::span<const float> p) { return p[2]; };
        r.add(std::move(glass));
    }
    r.add(local({"tone", "The layer in one colour: each pixel's brightness kept, its hue the tint's (a world grey while time stands still).",
           {{"color_r", 0.79f, 0, 1, "the tint, red"},
            {"color_g", 0.76f, 0, 1, "green"},
            {"color_b", 0.71f, 0, 1, "blue"},
            {"amount", 1, 0, 1, "how far toward the tint (0: as it is)"}},
           no_reach, tone, pixel(fx::tone::tone_fs)}));
}

}  // namespace

void run_cpu_effect(const EffectInfo& info, const EffectInput& in, Parallel* parallel) {
    if (!info.cpu) return;
    const int rows = in.last_row() - in.row_begin;
    constexpr int band = 32;
    if (!info.row_local || !parallel || rows <= band) {
        info.cpu(in);
        return;
    }
    const auto bands = static_cast<std::size_t>((rows + band - 1) / band);
    parallel->run(bands, [&](std::size_t i) {
        EffectInput part = in;
        part.row_begin = in.row_begin + static_cast<int>(i) * band;
        part.row_end = std::min(part.row_begin + band, in.last_row());
        info.cpu(part);
    });
}

float shape_distance(const EffectShape& shape, geom::Point p) noexcept {
    const geom::Rect& r = shape.rect;
    const float hx = r.width * 0.5f, hy = r.height * 0.5f;
    const float px = p.x - (r.x + hx), py = p.y - (r.y + hy);
    const geom::CornerRadii& c = shape.radii;
    float rad = px < 0 ? (py < 0 ? c.top_left : c.bottom_left) : (py < 0 ? c.top_right : c.bottom_right);
    rad = std::max(0.0f, std::min(rad, std::min(hx, hy)));
    const float qx = std::abs(px) - hx + rad, qy = std::abs(py) - hy + rad;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    const float outside = std::sqrt(ox * ox + oy * oy);
    return outside + std::min(std::max(qx, qy), 0.0f) - rad;
}

float smooth_min(float a, float b, float k) noexcept {
    if (!(k > 0)) return std::min(a, b);
    const float h = std::max(k - std::abs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

void box_blur(raster::Surface& surface, int radius, Parallel* parallel) {
    if (radius <= 0) return;
    raster::Surface scratch(surface.width(), surface.height());
    // Each pass's lines are independent: in groups on the pool, the same bytes.
    auto pass = [&](const Surface& from, Surface& to, bool horizontal) {
        const int lines = horizontal ? from.height() : from.width();
        constexpr int group = 64;
        if (!parallel || lines <= group) {
            box_pass(from, to, radius, horizontal);
            return;
        }
        parallel->run(static_cast<std::size_t>((lines + group - 1) / group), [&](std::size_t i) {
            box_pass(from, to, radius, horizontal, static_cast<int>(i) * group, static_cast<int>(i) * group + group);
        });
    };
    for (int i = 0; i < 3; ++i) {
        pass(surface, scratch, true);
        pass(scratch, surface, false);
    }
}

int box_radius(float logical, float scale) noexcept { return static_cast<int>(std::round(logical * scale / 3)); }

EffectRegistry::EffectRegistry() = default;

EffectRef EffectRegistry::add(EffectInfo info) {
    std::unique_lock g(lock_);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == info.name) return {static_cast<std::uint32_t>(i + 1)};
    entries_.push_back(std::move(info));
    generations_.push_back(0);
    return {static_cast<std::uint32_t>(entries_.size())};
}

bool EffectRegistry::replace(EffectRef ref, EffectInfo info) {
    std::unique_lock g(lock_);
    if (ref.id == 0 || ref.id > entries_.size()) return false;
    entries_[ref.id - 1] = std::move(info);
    ++generations_[ref.id - 1];
    return true;
}

std::uint64_t EffectRegistry::generation(EffectRef ref) const {
    std::shared_lock g(lock_);
    if (ref.id == 0 || ref.id > generations_.size()) return 0;
    return generations_[ref.id - 1];
}

std::optional<EffectRef> EffectRegistry::find(std::string_view name) const {
    std::shared_lock g(lock_);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].name == name) return EffectRef{static_cast<std::uint32_t>(i + 1)};
    return std::nullopt;
}

const EffectInfo* EffectRegistry::info(EffectRef ref) const {
    std::shared_lock g(lock_);
    if (ref.id == 0 || ref.id > entries_.size()) return nullptr;
    return &entries_[ref.id - 1];
}

std::vector<std::string_view> EffectRegistry::names() const {
    std::shared_lock g(lock_);
    std::vector<std::string_view> out;
    for (const EffectInfo& e : entries_) out.push_back(e.name);
    return out;
}

std::vector<float> EffectRegistry::params(EffectRef ref, std::span<const std::pair<std::string_view, float>> values) const {
    const EffectInfo* e = info(ref);
    if (!e) return {};
    std::vector<float> out;
    for (const EffectParam& p : e->params) {
        float v = p.value;
        for (const auto& [k, x] : values)
            if (k == p.name) v = std::clamp(x, p.min, p.max);
        out.push_back(v);
    }
    return out;
}

float EffectRegistry::reach(EffectRef ref, std::span<const float> params) const {
    const EffectInfo* e = info(ref);
    if (!e || !e->reach || params.size() < e->params.size()) return 0;
    return e->reach(params);
}

EffectRegistry& effects() {
    static EffectRegistry r;
    static const bool builtins = (register_builtins(r), true);  // once, in a fixed order: the same refs everywhere
    (void)builtins;
    return r;
}

}  // namespace omui::render
