module openmui.raster;

import std;
import hanim;

namespace omui::raster {

namespace {

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
        std::vector<Stop> stops(paint.stops.begin(), paint.stops.end());
        if (stops.empty()) stops.push_back({0, Color::transparent()});
        std::ranges::stable_sort(stops, {}, &Stop::offset);
        for (int i = 0; i < 256; ++i) {
            float t = static_cast<float>(i) / 255.0f;
            std::size_t k = 0;
            while (k + 1 < stops.size() && stops[k + 1].offset <= t) ++k;
            const Stop& a = stops[k];
            const Stop& b = k + 1 < stops.size() ? stops[k + 1] : stops[k];
            float span = b.offset - a.offset;
            float u = span > 0 ? std::clamp((t - a.offset) / span, 0.0f, 1.0f) : 0.0f;
            if (t < stops.front().offset) u = 0;
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
            double angle = hanim::dm::atan2(q.y - p.start.y, q.x - p.start.x) - p.radius;
            double turn = angle / hanim::dm::two_pi;
            t = static_cast<float>(turn - std::floor(turn));
            break;
        }
    }
    t = spread_t(t, p.spread);
    return lut_[static_cast<std::size_t>(static_cast<int>(t * 255.0f + 0.5f))];
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
    for (int i = 0; i < count; ++i) out[i] = gradient_at(static_cast<float>(x + i) + 0.5f, py);
}

}  // namespace omui::raster
