module openmui.raster;

import std;
import openmui.anim;

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
            double angle = anim::dm::atan2(q.y - p.start.y, q.x - p.start.x) - p.radius;
            double turn = angle / anim::dm::two_pi;
            t = static_cast<float>(turn - std::floor(turn));
            break;
        }
    }
    t = spread_t(t, p.spread);
    return lut_[static_cast<std::size_t>(static_cast<int>(t * 255.0f + 0.5f))];
}

Rgba8 Shader::image_at(float px, float py) const {
    const ImageView& img = paint_->image;
    if (!img.pixels || img.width <= 0 || img.height <= 0) return {};
    Point q = inverse_->apply({px, py});
    Rgba8 c;
    if (paint_->sampling == Sampling::nearest) {
        int x = std::clamp(static_cast<int>(std::floor(q.x)), 0, img.width - 1);
        int y = std::clamp(static_cast<int>(std::floor(q.y)), 0, img.height - 1);
        c = img.at(x, y);
    } else {
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
        c = {mix(a.r, b.r, cc.r, d.r), mix(a.g, b.g, cc.g, d.g), mix(a.b, b.b, cc.b, d.b), mix(a.a, b.a, cc.a, d.a)};
    }
    std::uint32_t tint = static_cast<std::uint32_t>(std::clamp(paint_->color.a, 0.0f, 1.0f) * 255.0f + 0.5f);
    std::uint32_t k = mul255(tint, opacity8_);
    if (k != 255) c = {static_cast<std::uint8_t>(mul255(c.r, k)), static_cast<std::uint8_t>(mul255(c.g, k)),
                       static_cast<std::uint8_t>(mul255(c.b, k)), static_cast<std::uint8_t>(mul255(c.a, k))};
    return c;
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
    for (int i = 0; i < count; ++i) {
        float px = static_cast<float>(x + i) + 0.5f;
        out[i] = paint_->kind == Paint::Kind::gradient ? gradient_at(px, py) : image_at(px, py);
    }
}

}  // namespace omui::raster
