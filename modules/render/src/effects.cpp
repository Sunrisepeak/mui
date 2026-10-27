// The built-in pixel effects' CPU reference implementations. Every one is a
// pure function of its inputs: integer hashing for randomness, anim's
// deterministic sine and cosine, no fused multiply-add — the same bytes on
// every platform and compiler (spec R-2).
module openmui.render;

import std;
import openmui.geom;
import openmui.raster;
import openmui.anim;
import :display;
import :effects;

namespace omui::render {

namespace {

using raster::Rgba8;
using raster::Surface;

std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
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
    for (int y = 0; y < in.target.height(); ++y) {
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
    const double a = static_cast<double>(params[1]) * anim::dm::pi / 180.0;
    const auto c = static_cast<float>(anim::dm::cos(a)), sn = static_cast<float>(anim::dm::sin(a));
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
    const double arg = 2 * anim::dm::pi * ((static_cast<double>(y) + 0.5) / wavelength + phase);
    return amount * static_cast<float>(anim::dm::sin(arg));
}

void displace(const EffectInput& in) {
    for (int y = 0; y < in.target.height(); ++y) {
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
    const double a = static_cast<double>(params[1]) * anim::dm::pi / 180.0;
    return {params[0] * scale * static_cast<float>(anim::dm::cos(a)), params[0] * scale * static_cast<float>(anim::dm::sin(a))};
}

void chromatic(const EffectInput& in) {
    const auto [ox, oy] = chromatic_offset(in.params, in.scale);
    for (int y = 0; y < in.target.height(); ++y) {
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

// Box blur of radius r along one axis, in integers (sums of premultiplied bytes).
void box_pass(const Surface& from, Surface& to, int r, bool horizontal) {
    const int w = from.width(), h = from.height();
    const int n = 2 * r + 1;
    const int lines = horizontal ? h : w, length = horizontal ? w : h;
    for (int line = 0; line < lines; ++line) {
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

int blur_radius(std::span<const float> params, float scale) { return static_cast<int>(std::round(params[0] * scale / 3)); }

void blur(const EffectInput& in) {
    const int r = blur_radius(in.params, in.scale);
    if (r <= 0) {
        each_pixel(in, [](int, int, Rgba8 s) { return s; });
        return;
    }
    // Three box passes each way approach a Gaussian of the given radius.
    Surface a = in.source, b(in.source.width(), in.source.height());
    for (int i = 0; i < 3; ++i) {
        box_pass(a, b, r, true);
        box_pass(b, a, r, false);
    }
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

void register_builtins(EffectRegistry& r) {
    r.add({"dissolve", "Pixels (or cells of `grain` pixels) vanish in a seeded random order as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: all shown; 1: all gone"},
            {"seed", 1, 0, 1e6f, "which random order"},
            {"softness", 0.15f, 0.001f, 1, "how gradually each pixel fades"},
            {"grain", 1, 1, 64, "cell size in logical pixels"}},
           no_reach, dissolve, pixel(fx::dissolve::dissolve_fs, derive_dissolve)});
    r.add({"wipe", "The layer is wiped away from one edge as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: all shown; 1: all gone"},
            {"angle", 0, -360, 360, "direction of travel in degrees (0: left to right)"},
            {"softness", 0.1f, 0.001f, 1, "width of the edge"}},
           no_reach, wipe, pixel(fx::wipe::wipe_fs, derive_wipe)});
    r.add({"reveal", "A circle grows from a point and shows the layer as progress goes from 0 to 1.",
           {{"progress", 0, 0, 1, "0: hidden; 1: all shown"},
            {"x", 0.5f, 0, 1, "centre, as a fraction of the layer's width"},
            {"y", 0.5f, 0, 1, "centre, as a fraction of the layer's height"},
            {"softness", 0.08f, 0.001f, 1, "width of the edge"}},
           no_reach, reveal, pixel(fx::reveal::reveal_fs, derive_reveal)});
    r.add({"displace", "Rows shift sideways along a sine wave (heat haze, water, a flag).",
           {{"amount", 4, 0, 200, "largest shift in logical pixels"},
            {"wavelength", 40, 1, 2000, "wave length in logical pixels"},
            {"phase", 0, 0, 1, "position of the wave (animate it to make it travel)"}},
           [](std::span<const float> p) { return std::abs(p[0]); }, displace, pixel(fx::displace::displace_fs, nullptr, rows_displace)});
    r.add({"noise", "Seeded grain over the colours.",
           {{"amount", 0.15f, 0, 1, "strength"}, {"seed", 1, 0, 1e6f, "which grain"}}, no_reach, noise, pixel(fx::noise::noise_fs)});
    r.add({"chromatic", "Red and blue drift apart from green, as through a cheap lens.",
           {{"offset", 2, 0, 100, "distance in logical pixels"}, {"angle", 0, -360, 360, "direction in degrees"}},
           [](std::span<const float> p) { return std::abs(p[0]); }, chromatic, pixel(fx::chromatic::chromatic_fs, derive_chromatic)});
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
}

}  // namespace

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
