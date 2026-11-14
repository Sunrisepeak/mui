module xrender.raster.api;

import std;
import mlibs.math;

namespace xrender::raster {

namespace {

// The ramp's colour `weight` (in 1/65536) of the way from `entry` to the next: (here × (65536 − weight) + next ×
// weight + 32768) >> 16 a channel — as here × 65536 + 32768 + (next − here) × weight, two channels a 64-bit word
// and a multiplication; at a hard stop's edge the entry's on its side of it.
constexpr float ramp_units = 255.0f * 65536.0f;

template <class Wide>
[[gnu::always_inline]] inline Rgba8 ramp_between(const std::array<Wide, 256>& wide, const std::array<std::uint8_t, 256>& hard,
                                                 int entry, int weight) {
    // An entry with an edge after it steps by nothing: past the edge it is the next entry's own colour.
    auto index = static_cast<std::size_t>(entry);
    if (const int met = hard[index]; met != 0 && weight >= met * 256) [[unlikely]] index = std::min<std::size_t>(index + 1, 255), weight = 0;
    const Wide&         here = wide[index];
    const auto          by = static_cast<std::uint64_t>(weight);
    const std::uint64_t red_blue = here.red_blue + here.red_blue_step * by, green_alpha = here.green_alpha + here.green_alpha_step * by;
    return {static_cast<std::uint8_t>(red_blue >> 16), static_cast<std::uint8_t>(green_alpha >> 16), static_cast<std::uint8_t>(red_blue >> 48),
            static_cast<std::uint8_t>(green_alpha >> 48)};
}

float spread_t(float t, Spread s) {
    switch (s) {
        case Spread::pad: return std::clamp(t, 0.0f, 1.0f);
        case Spread::repeat: return t - std::floor(t);
        case Spread::reflect: {
            float m = t - 2.0f * std::floor(t / 2.0f);
            return m > 1.0f ? 2.0f - m : m;
        }
    }
    return t;
}

}  // namespace

namespace {

// An axis-aligned image's columns, the same for every row of one fill: each
// pixel column's two source columns and its weight. Kept per thread (grown,
// never freed: no allocation a frame), for the shader whose serial it holds.
struct Columns {
    std::uint64_t              serial = 0;
    int                        x = 0, count = 0;
    std::vector<std::uint32_t> taps;  // per column: xa, xb, wx
};
thread_local Columns columns;
std::atomic<std::uint64_t> serials{0};

}  // namespace

Shader::Shader(const Paint& paint) : paint_(&paint) {
    opacity8_ = static_cast<std::uint32_t>(std::clamp(paint.opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    if (paint.kind == Paint::Kind::solid) {
        solid_ = true;
        Color c = paint.color;
        c.a *= std::clamp(paint.opacity, 0.0f, 1.0f);
        solid_color_ = premultiply(c);
        return;
    }
    inverse_ = paint.transform.inverse();
    if (paint.kind == Paint::Kind::image) serial_ = ++serials;
    image_k_ = mul255(static_cast<std::uint32_t>(std::clamp(paint.color.a, 0.0f, 1.0f) * 255.0f + 0.5f), opacity8_);
    if (paint.kind == Paint::Kind::image_mask) solid_color_ = premultiply(paint.color);  // the mask's colour (not a solid paint)
    if (paint.material && (paint.kind == Paint::Kind::image || paint.kind == Paint::Kind::image_mask))
        material_ = material_setup(*paint.material, paint.transform);
    if (paint.kind == Paint::Kind::gradient) {
        // The table: premultiplied stops, interpolated linearly in premultiplied sRGB.
        const Stop transparent{0, Color::transparent()};
        const std::span<const Stop> stops = paint.stops.empty() ? std::span(&transparent, 1) : paint.stops;
        const bool ordered = std::ranges::is_sorted(stops, {}, &Stop::offset);
        const Stop* first = &stops.front();
        if (!ordered) for (const auto& stop : stops) if (stop.offset < first->offset) first = &stop;
        std::size_t cursor = 0;
        for (int i = 0; i < 256; ++i) {
            float t = static_cast<float>(i) / 255.0f;
            const Stop* left = first;
            const Stop* right = nullptr;
            if (ordered) {
                while (cursor + 1 < stops.size() && stops[cursor + 1].offset <= t) ++cursor;
                left = &stops[cursor];
                right = cursor + 1 < stops.size() ? &stops[cursor + 1] : left;
            } else {
                // Same stable tie rule as stable_sort, without copying,
                // allocating or sorting per frame. Below a stop's offset the
                // FIRST tied stop wins; at the offset the LAST tied stop wins.
                bool found = false;
                for (const auto& stop : stops) {
                    if (stop.offset <= t) {
                        if (!found || stop.offset >= left->offset) left = &stop;
                        found = true;
                    } else if (!right || stop.offset < right->offset) right = &stop;
                }
                if (!right) right = left;
            }
            const Stop& a = *left;
            const Stop& b = *right;
            float span = b.offset - a.offset;
            float u = span > 0 ? std::clamp((t - a.offset) / span, 0.0f, 1.0f) : 0.0f;
            if (t < first->offset) u = 0;
            float aa = a.color.a, ba = b.color.a;
            float r = (a.color.r * aa) * (1 - u) + (b.color.r * ba) * u;
            float g = (a.color.g * aa) * (1 - u) + (b.color.g * ba) * u;
            float bl = (a.color.b * aa) * (1 - u) + (b.color.b * ba) * u;
            float al = aa * (1 - u) + ba * u;
            al *= std::clamp(paint.opacity, 0.0f, 1.0f);
            float op = std::clamp(paint.opacity, 0.0f, 1.0f);
            auto q = [](float v) { return static_cast<std::uint8_t>(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); };
            lut_[static_cast<std::size_t>(i)] = {q(r * op), q(g * op), q(bl * op), q(al)};
        }
        // Stops that meet at one offset after an entry and not after the next: an edge between the two.
        for (std::size_t one = 0; one < stops.size(); ++one)
            for (std::size_t other = one + 1; other < stops.size(); ++other) {
                const float offset = stops[one].offset;
                if (stops[other].offset != offset || !(offset > 0.0f && offset <= 1.0f)) continue;
                const int entry = std::clamp(static_cast<int>(std::ceil(offset * 255.0f)) - 1, 0, 254);
                // Where between the two, as the weight it is met at.
                const int met = static_cast<int>((offset * 255.0f - static_cast<float>(entry)) * 256.0f + 0.5f);
                hard_[static_cast<std::size_t>(entry)] = static_cast<std::uint8_t>(std::clamp(met, 1, 255));
            }
        bool gentle = true;
        for (std::size_t entry = 0; entry < wide_.size(); ++entry) {
            const Rgba8 here = lut_[entry], next = lut_[std::min<std::size_t>(entry + 1, 255)];
            auto pair = [](std::int64_t low, std::int64_t high) { return static_cast<std::uint64_t>(low) + (static_cast<std::uint64_t>(high) << 32); };
            const bool edge = hard_[entry] != 0;
            // The half to round by: one less where the channel falls, so that half way is the next entry's
            // whichever way it goes — and a step of one level is taken exactly where the nearer entry changes.
            auto begun = [](std::uint8_t from, std::uint8_t to) { return from * 65536 + (to < from ? 32767 : 32768); };
            wide_[entry] = {pair(begun(here.r, next.r), begun(here.b, next.b)), pair(begun(here.g, next.g), begun(here.a, next.a)),
                            edge ? 0 : pair(next.r - here.r, next.b - here.b), edge ? 0 : pair(next.g - here.g, next.a - here.a)};
            auto level = [](std::uint8_t from, std::uint8_t to) { return std::abs(to - from) <= 1; };
            gentle = gentle && !edge && level(here.r, next.r) && level(here.g, next.g) && level(here.b, next.b) && level(here.a, next.a);
        }
        gentle_ = gentle;
        opaque_ = inverse_.has_value() && std::ranges::all_of(lut_, [](const Rgba8& c) { return c.a == 255; });
    }
}

Rgba8 Shader::gradient_at(float px, float py) const {
    const Paint& p = *paint_;
    Point q = inverse_->apply({px, py});
    float t = 0;
    switch (p.gradient) {
        case GradientKind::linear: {
            float dx = p.end.x - p.start.x, dy = p.end.y - p.start.y;
            float len2 = dx * dx + dy * dy;
            t = len2 > 0 ? ((q.x - p.start.x) * dx + (q.y - p.start.y) * dy) / len2 : 0;
            break;
        }
        case GradientKind::radial: {
            float dx = q.x - p.start.x, dy = q.y - p.start.y;
            t = p.radius > 0 ? std::sqrt(dx * dx + dy * dy) / p.radius : 1;
            break;
        }
        case GradientKind::conic: {
            double angle = mlibs::math::det::atan2(q.y - p.start.y, q.x - p.start.x) - p.radius;
            double turn = angle / mlibs::math::det::two_pi;
            t = static_cast<float>(turn - std::floor(turn));
            break;
        }
    }
    const int at = static_cast<int>(spread_t(t, p.spread) * ramp_units + 0.5f);  // ramp_colour's, here in the pixel's loop
    if (gentle_) return lut_[static_cast<std::size_t>((at + 32768) >> 16)];
    return ramp_between(wide_, hard_, at >> 16, at & 0xffff);
}

Rgba8 Shader::ramp_colour(float t) const noexcept {
    // Where along the table in 1/65536 of an entry (a float's 24 bits hold it): the entry and the weight to the next.
    const int at = static_cast<int>(t * ramp_units + 0.5f);
    if (gentle_) return lut_[static_cast<std::size_t>((at + 32768) >> 16)];
    return ramp_between(wide_, hard_, at >> 16, at & 0xffff);
}

Rgba8 Shader::texel_at(const ImageView& img, Point q) const {
    if (paint_->sampling == Sampling::nearest) {
        int x = std::clamp(static_cast<int>(std::floor(q.x)), 0, img.width - 1);
        int y = std::clamp(static_cast<int>(std::floor(q.y)), 0, img.height - 1);
        return img.at(x, y);
    }
    // Bilinear, edges clamped, weights in 1/256.
    float fx = q.x - 0.5f, fy = q.y - 0.5f;
    int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    auto wx = static_cast<std::uint32_t>(static_cast<int>((fx - static_cast<float>(x0)) * 256.0f + 0.5f));
    auto wy = static_cast<std::uint32_t>(static_cast<int>((fy - static_cast<float>(y0)) * 256.0f + 0.5f));
    wx = std::min(wx, 256u);
    wy = std::min(wy, 256u);
    auto at = [&](int x, int y) -> const Rgba8& {
        return img.at(std::clamp(x, 0, img.width - 1), std::clamp(y, 0, img.height - 1));
    };
    const Rgba8 &a = at(x0, y0), &b = at(x0 + 1, y0), &cc = at(x0, y0 + 1), &d = at(x0 + 1, y0 + 1);
    auto mix = [&](std::uint8_t va, std::uint8_t vb, std::uint8_t vc, std::uint8_t vd) {
        std::uint32_t top = va * (256 - wx) + vb * wx;
        std::uint32_t bottom = vc * (256 - wx) + vd * wx;
        std::uint32_t v = top * (256 - wy) + bottom * wy;
        return static_cast<std::uint8_t>((v + 32768) >> 16);
    };
    return {mix(a.r, b.r, cc.r, d.r), mix(a.g, b.g, cc.g, d.g), mix(a.b, b.b, cc.b, d.b), mix(a.a, b.a, cc.a, d.a)};
}

namespace {

[[gnu::always_inline]] inline Rgba8 scaled8(Rgba8 c, std::uint32_t k) noexcept {
    if (k == 255) return c;
    return {static_cast<std::uint8_t>(mul255(c.r, k)), static_cast<std::uint8_t>(mul255(c.g, k)), static_cast<std::uint8_t>(mul255(c.b, k)),
            static_cast<std::uint8_t>(mul255(c.a, k))};
}

}  // namespace

// An image's row of pixels: the texel under each pixel's centre, tinted. When the transform neither
// rotates nor skews (b = c = 0, a page's pictures and the atlas's textures
// as a camera flies over them), a pixel's source column depends on its column
// alone and its source row on its row alone: the rows and their weight are
// found once, a column's texels read without clamping where they are inside
// — the same bytes as the general path.
void Shader::shade_image(int x, int y, int count, Rgba8* out) const {
    const ImageView& img = paint_->image;
    if (!img.pixels || img.width <= 0 || img.height <= 0) {
        std::fill_n(out, count, Rgba8{});
        return;
    }
    const Affine& m = *inverse_;
    const float py = static_cast<float>(y) + 0.5f;
    if (m.b != 0 || m.c != 0) {
        for (int i = 0; i < count; ++i) out[i] = scaled8(texel_at(img, m.apply({static_cast<float>(x + i) + 0.5f, py})), image_k_);
        return;
    }
    // q = (a·px + c·py + e, b·px + d·py + f) with b = c = 0: the zero terms add
    // nothing, so these are the same floats apply() computes.
    const float qy = m.d * py + m.f;
    const int   last_x = img.width - 1, last_y = img.height - 1;
    if (paint_->sampling == Sampling::nearest) {
        const Rgba8* row = img.pixels + static_cast<std::size_t>(std::clamp(static_cast<int>(std::floor(qy)), 0, last_y)) * img.stride;
        for (int i = 0; i < count; ++i) {
            const float qx = m.a * (static_cast<float>(x + i) + 0.5f) + m.e;
            out[i] = scaled8(row[std::clamp(static_cast<int>(std::floor(qx)), 0, last_x)], image_k_);
        }
        return;
    }
    const float fy = qy - 0.5f;
    const int   y0 = static_cast<int>(std::floor(fy));
    const std::uint32_t wy = std::min(static_cast<std::uint32_t>(static_cast<int>((fy - static_cast<float>(y0)) * 256.0f + 0.5f)), 256u);
    const Rgba8* top = img.pixels + static_cast<std::size_t>(std::clamp(y0, 0, last_y)) * img.stride;
    const Rgba8* bottom = img.pixels + static_cast<std::size_t>(std::clamp(y0 + 1, 0, last_y)) * img.stride;
    // The columns: found on this fill's first row, read on the others.
    Columns& cols = columns;
    if (cols.serial != serial_ || cols.x != x || cols.count != count) {
        cols.serial = serial_;
        cols.x = x;
        cols.count = count;
        cols.taps.resize(static_cast<std::size_t>(count) * 3);
        for (int i = 0; i < count; ++i) {
            const float fx = (m.a * (static_cast<float>(x + i) + 0.5f) + m.e) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            cols.taps[static_cast<std::size_t>(i) * 3] = static_cast<std::uint32_t>(std::clamp(x0, 0, last_x));
            cols.taps[static_cast<std::size_t>(i) * 3 + 1] = static_cast<std::uint32_t>(std::clamp(x0 + 1, 0, last_x));
            cols.taps[static_cast<std::size_t>(i) * 3 + 2] =
                std::min(static_cast<std::uint32_t>(static_cast<int>((fx - static_cast<float>(x0)) * 256.0f + 0.5f)), 256u);
        }
    }
    const std::uint32_t* tap = cols.taps.data();
    const auto lerp = [](std::uint32_t wx, std::uint8_t va, std::uint8_t vb) { return va * (256 - wx) + vb * wx; };
    const auto put = [&](int i, auto channel) {
        out[i] = scaled8({channel(0), channel(1), channel(2), channel(3)}, image_k_);
    };
    if (wy == 0) {
        // On a source row: the bottom row's weight is 0, (t·256 + 32768) >> 16 as the general mix.
        for (int i = 0; i < count; ++i, tap += 3) {
            const auto* a = reinterpret_cast<const std::uint8_t*>(top + tap[0]);
            const auto* b = reinterpret_cast<const std::uint8_t*>(top + tap[1]);
            const std::uint32_t wx = tap[2];
            put(i, [&](int k) { return static_cast<std::uint8_t>((lerp(wx, a[k], b[k]) * 256 + 32768) >> 16); });
        }
        return;
    }
    for (int i = 0; i < count; ++i, tap += 3) {
        const auto* a = reinterpret_cast<const std::uint8_t*>(top + tap[0]);
        const auto* b = reinterpret_cast<const std::uint8_t*>(top + tap[1]);
        const auto* c = reinterpret_cast<const std::uint8_t*>(bottom + tap[0]);
        const auto* d = reinterpret_cast<const std::uint8_t*>(bottom + tap[1]);
        const std::uint32_t wx = tap[2];
        put(i, [&](int k) {
            return static_cast<std::uint8_t>((lerp(wx, a[k], b[k]) * (256 - wy) + lerp(wx, c[k], d[k]) * wy + 32768) >> 16);
        });
    }
}

// An image_mask's or a lit image's pixel: the texel (as a mask, the colour
// times its alpha), lit, then the tint and opacity — the order the GPU keeps.
Rgba8 Shader::shaded_at(float px, float py) const {
    const ImageView& img = paint_->image;
    if (!img.pixels || img.width <= 0 || img.height <= 0) return {};
    const Point q = inverse_->apply({px, py});
    Rgba8 c = texel_at(img, q);
    std::uint32_t k = opacity8_;
    if (paint_->kind == Paint::Kind::image_mask) c = scaled8(solid_color_, c.a);
    else k = mul255(static_cast<std::uint32_t>(std::clamp(paint_->color.a, 0.0f, 1.0f) * 255.0f + 0.5f), opacity8_);
    if (material_) {
        const ImageView& nm = paint_->material->normal;
        if (nm.pixels && nm.width > 0 && nm.height > 0) {
            // The normal map stretched over the image (the same size: the same place).
            const Point qn = nm.width == img.width && nm.height == img.height
                                 ? q
                                 : Point{q.x * (static_cast<float>(nm.width) / static_cast<float>(img.width)),
                                         q.y * (static_cast<float>(nm.height) / static_cast<float>(img.height))};
            c = lit(c, texel_at(nm, qn), *material_);
        }
    }
    return scaled8(c, k);
}

std::array<float, 4> orthogonal_part(const Affine& m) noexcept {
    // M = [a c; b d]. Its nearest rotation is [p −q; q p]/|·| with p = a + d,
    // q = b − c; its nearest reflection [p q; q −p]/|·| with p = a − d, q = b + c.
    const bool mirrored = m.a * m.d - m.b * m.c < 0;
    const float p = mirrored ? m.a - m.d : m.a + m.d;
    const float q = mirrored ? m.b + m.c : m.b - m.c;
    const float len = std::sqrt(p * p + q * q);
    if (!(len > 0)) return {1, 0, 0, 1};
    const float c = p / len, s = q / len;
    return mirrored ? std::array<float, 4>{c, s, s, -c} : std::array<float, 4>{c, -s, s, c};
}

MaterialSetup material_setup(const Material& m, const Affine& transform) noexcept {
    MaterialSetup s;
    const float len = std::sqrt(m.light_x * m.light_x + m.light_y * m.light_y + m.light_z * m.light_z);
    if (len > 0) {
        s.lx = m.light_x / len;
        s.ly = m.light_y / len;
        s.lz = m.light_z / len;
    }
    const float flat = std::sqrt(m.light_x * m.light_x + m.light_y * m.light_y);
    if (flat > 0) {
        s.fx = m.light_x / flat;
        s.fy = m.light_y / flat;
    }
    const auto r = orthogonal_part(transform);
    s.r00 = r[0];
    s.r01 = r[1];
    s.r10 = r[2];
    s.r11 = r[3];
    s.shadow_r = std::clamp(m.shadow.r, 0.0f, 1.0f);
    s.shadow_g = std::clamp(m.shadow.g, 0.0f, 1.0f);
    s.shadow_b = std::clamp(m.shadow.b, 0.0f, 1.0f);
    s.shadow_strength = std::clamp(m.shadow_strength, 0.0f, 1.0f);
    const float ra = std::clamp(m.rim.a, 0.0f, 1.0f);
    s.rim_r = std::clamp(m.rim.r, 0.0f, 1.0f) * ra;
    s.rim_g = std::clamp(m.rim.g, 0.0f, 1.0f) * ra;
    s.rim_b = std::clamp(m.rim.b, 0.0f, 1.0f) * ra;
    s.rim_strength = std::max(m.rim_strength, 0.0f);
    s.rim_width = std::clamp(m.rim_width, 0.0f, 1.0f);
    s.key_r = std::clamp(m.key.r, 0.0f, 1.0f);
    s.key_g = std::clamp(m.key.g, 0.0f, 1.0f);
    s.key_b = std::clamp(m.key.b, 0.0f, 1.0f);
    s.inv_softness = m.softness > 0 ? 1.0f / m.softness : 0.0f;
    s.bands = m.bands >= 2 ? std::min(m.bands, 16) : 0;
    if (s.bands >= 2) {
        s.inv_levels = 1.0f / static_cast<float>(s.bands);
        s.inv_steps = 1.0f / static_cast<float>(s.bands - 1);
    }
    return s;
}

// The shading, in IEEE single precision without contraction (so the same
// bits everywhere; one square root and one division a pixel, the rest
// products of constants material_setup made): the texel's normal (128 is
// zero, ±127 is ±1) turned into device space and made unit; lambert n·l;
// cut into bands, each edge a linear ramp `softness` wide centred on its
// threshold i / bands; the colour
// multiplied toward the shadow colour by (1 − lit)·strength; a rim where the
// normal grazes (n·z below the width, its edge as soft as a band's), as
// strong as the normal faces the light in the screen's plane, added
// premultiplied; clamped to the alpha.
Rgba8 Shader::lit(Rgba8 c, Rgba8 n, const MaterialSetup& m) noexcept {
    if (c.a == 0) return c;
    constexpr float unit = 1.0f / 127.0f;
    const float nx = (static_cast<float>(n.r) - 128.0f) * unit;
    const float ny = (static_cast<float>(n.g) - 128.0f) * unit;
    const float nz = (static_cast<float>(n.b) - 128.0f) * unit;
    float tx = m.r00 * nx + m.r01 * ny;
    float ty = m.r10 * nx + m.r11 * ny;
    float tz = nz;
    const float len2 = tx * tx + ty * ty + tz * tz;
    if (len2 > 0) {
        const float inv = 1.0f / std::sqrt(len2);
        tx = tx * inv;
        ty = ty * inv;
        tz = tz * inv;
    } else {
        tz = 1;
    }
    const float lambert = std::max(tx * m.lx + ty * m.ly + tz * m.lz, 0.0f);
    float light = lambert;
    if (m.bands >= 2) {
        float sum = 0;
        for (int i = 1; i < m.bands; ++i) {
            const float t = static_cast<float>(i) * m.inv_levels;
            sum += m.inv_softness > 0 ? std::clamp((lambert - t) * m.inv_softness + 0.5f, 0.0f, 1.0f) : (lambert >= t ? 1.0f : 0.0f);
        }
        light = sum * m.inv_steps;
    }
    const float shade = (1.0f - light) * m.shadow_strength;
    // The lit part in the light's colour, the shaded part toward the shadow's (a white light: exactly the painted colour).
    const float kr = (1.0f - shade) * m.key_r + shade * m.shadow_r;
    const float kg = (1.0f - shade) * m.key_g + shade * m.shadow_g;
    const float kb = (1.0f - shade) * m.key_b + shade * m.shadow_b;
    float rim = 0;
    if (m.rim_strength > 0 && m.rim_width > 0) {
        const float past = (1.0f - std::max(tz, 0.0f)) - (1.0f - m.rim_width);  // how far the grazing is past the rim's edge
        const float e = m.inv_softness > 0 ? std::clamp(past * m.inv_softness + 0.5f, 0.0f, 1.0f) : (past >= 0 ? 1.0f : 0.0f);
        rim = m.rim_strength * e * std::max(tx * m.fx + ty * m.fy, 0.0f);
    }
    const float a = static_cast<float>(c.a);
    auto channel = [&](std::uint8_t v, float k, float rc) {
        const float o = std::clamp(static_cast<float>(v) * k + rc * rim * a, 0.0f, a);
        return static_cast<std::uint8_t>(static_cast<int>(o + 0.5f));
    };
    return {channel(c.r, kr, m.rim_r), channel(c.g, kg, m.rim_g), channel(c.b, kb, m.rim_b), c.a};
}

namespace {
// Four float lanes (GNU vector types: one SSE or wasm SIMD register, each
// lane computed as the scalar code computes it).
using Lanes [[gnu::vector_size(16)]] = float;
using LaneInts [[gnu::vector_size(16)]] = std::int32_t;
constexpr LaneInts lane_index{0, 1, 2, 3};
}  // namespace

void Shader::shade(int x, int y, int count, Rgba8* out) const {
    if (solid_) {
        std::fill_n(out, count, solid_color_);
        return;
    }
    if (!inverse_) {
        std::fill_n(out, count, Rgba8{});
        return;
    }
    float py = static_cast<float>(y) + 0.5f;
    if (material_ || paint_->kind == Paint::Kind::image_mask) {
        for (int i = 0; i < count; ++i) out[i] = shaded_at(static_cast<float>(x + i) + 0.5f, py);
        return;
    }
    if (paint_->kind == Paint::Kind::image) {
        shade_image(x, y, count, out);
        return;
    }
    if (paint_->gradient == GradientKind::linear && paint_->spread == Spread::pad) {
        // A linear, padded gradient a row at a time: gradient_at's own float
        // operations in its order (apply(), then the projection, clamp and
        // table), with what is the same for every pixel computed once — so
        // the same bytes, in a loop the compiler can keep in vector registers.
        const Paint& p = *paint_;
        const Affine& m = *inverse_;
        const float dx = p.end.x - p.start.x, dy = p.end.y - p.start.y;
        const float len2 = dx * dx + dy * dy;
        if (!(len2 > 0)) {
            std::fill_n(out, count, lut_[0]);
            return;
        }
        const float cy = m.c * py, dyy = m.d * py;
        // Four pixels' table entries at once: the same operations in the
        // same order a lane each (IEEE, no fusing), so the same bytes; then
        // the four lookups.
        int i = 0;
        for (; i + 4 <= count; i += 4) {
            Lanes px = __builtin_convertvector(lane_index + (x + i), Lanes) + 0.5f;
            const Lanes qx = m.a * px + cy + m.e, qy = m.b * px + dyy + m.f;
            Lanes t = ((qx - p.start.x) * dx + (qy - p.start.y) * dy) / len2;
            t = t < 0.0f ? Lanes{} : t;          // std::clamp's own tests, in its order
            t = 1.0f < t ? Lanes{} + 1.0f : t;
            // ramp_colour's entry and weight, the four at once.
            const LaneInts at = __builtin_convertvector(t * ramp_units + 0.5f, LaneInts);
            if (gentle_) {
                const LaneInts nearer = (at + 32768) >> 16;
                for (int lane = 0; lane < 4; ++lane) out[i + lane] = lut_[static_cast<std::size_t>(nearer[lane])];
            } else {
                for (int lane = 0; lane < 4; ++lane) out[i + lane] = ramp_between(wide_, hard_, at[lane] >> 16, at[lane] & 0xffff);
            }
        }
        for (; i < count; ++i) {
            const float px = static_cast<float>(x + i) + 0.5f;
            const float qx = m.a * px + cy + m.e, qy = m.b * px + dyy + m.f;
            const float t = std::clamp(((qx - p.start.x) * dx + (qy - p.start.y) * dy) / len2, 0.0f, 1.0f);
            const int at = static_cast<int>(t * ramp_units + 0.5f);
            out[i] = gentle_ ? lut_[static_cast<std::size_t>((at + 32768) >> 16)] : ramp_between(wide_, hard_, at >> 16, at & 0xffff);
        }
        return;
    }
    for (int i = 0; i < count; ++i) out[i] = gradient_at(static_cast<float>(x + i) + 0.5f, py);
}

namespace {
Rgba8 mix_bytes(const std::array<float, 4>& sums, std::uint32_t opacity) {
    auto byte = [opacity](float value) {
        const auto result = static_cast<std::uint32_t>(std::clamp(value, 0.0f, 255.0f) + 0.5f);
        return static_cast<std::uint8_t>(mul255(result, opacity));
    };
    return {byte(sums[0]), byte(sums[1]), byte(sums[2]), byte(sums[3])};
}
}
bool MixShader::reset(const Paint& paint) {
    if (paint.kind != Paint::Kind::mix || paint.mix.empty() || paint.mix.size() > max_paint_mix_terms ||
        !std::isfinite(paint.opacity) || paint.opacity < 0 || paint.opacity > 1) return false;
    double total = 0;
    for (const auto& term : paint.mix) {
        if (term.paint.kind == Paint::Kind::mix || !std::isfinite(term.weight) || term.weight < 0 || term.weight > 1) return false;
        if (term.paint.kind != Paint::Kind::solid && term.paint.kind != Paint::Kind::gradient &&
            term.paint.kind != Paint::Kind::image && term.paint.kind != Paint::Kind::image_mask) return false;
        total += term.weight;
    }
    if (std::abs(total - 1) > 1e-5) return false;
    opacity_ = static_cast<std::uint32_t>(paint.opacity * 255 + 0.5f);
    shaders_.clear();
    paints_.resize(paint.mix.size()); weights_.resize(paint.mix.size());
    shaders_.reserve(paint.mix.size());
    solid_ = true;
    std::array<float, 4> sum{};
    for (std::size_t i = 0; i < paint.mix.size(); ++i) {
        paints_[i] = paint.mix[i].paint;
        paints_[i].transform = paint.transform * paints_[i].transform;
        weights_[i] = static_cast<float>(paint.mix[i].weight / total);
        shaders_.emplace_back(paints_[i]);
        solid_ = solid_ && shaders_.back().solid();
        if (shaders_.back().solid()) {
            const auto c = shaders_.back().solid_color(); const auto w = weights_[i];
            sum[0] += w * c.r; sum[1] += w * c.g; sum[2] += w * c.b; sum[3] += w * c.a;
        }
    }
    color_ = solid_ ? mix_bytes(sum, opacity_) : Rgba8{};
    return true;
}
void MixShader::shade(int x, int y, int count, Rgba8* output) const {
    if (solid_) { std::fill_n(output, count, color_); return; }
    row_.resize(static_cast<std::size_t>(count));
    sums_.assign(static_cast<std::size_t>(count), std::array<float, 4>{});
    for (std::size_t i = 0; i < shaders_.size(); ++i) {
        shaders_[i].shade(x, y, count, row_.data());
        const float w = weights_[i];
        for (int j = 0; j < count; ++j) {
            auto& sum = sums_[static_cast<std::size_t>(j)]; const auto c = row_[static_cast<std::size_t>(j)];
            sum[0] += w * c.r; sum[1] += w * c.g; sum[2] += w * c.b; sum[3] += w * c.a;
        }
    }
    for (int j = 0; j < count; ++j) output[j] = mix_bytes(sums_[static_cast<std::size_t>(j)], opacity_);
}

}  // namespace xrender::raster
