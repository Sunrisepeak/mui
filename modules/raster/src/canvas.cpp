module openmui.raster;

import std;

namespace omui::raster {

namespace {

PixelRect intersect(PixelRect a, PixelRect b) {
    std::int32_t l = std::max(a.x, b.x), t = std::max(a.y, b.y);
    std::int32_t r = std::min(a.x + a.width, b.x + b.width), bb = std::min(a.y + a.height, b.y + b.height);
    if (r <= l || bb <= t) return {l, t, 0, 0};
    return {l, t, r - l, bb - t};
}

// A shape for clips and shadows.
void rounded_rect_path(PathBuilder& pb, const Rect& r, const CornerRadii& radii) {
    if (radii.zero()) {
        pb.rect(r);
        return;
    }
    CornerRadii f = radii.fitted(r.size());
    constexpr float k = 0.5522847498307936f;
    float l = r.x, t = r.y, rr = r.right(), b = r.bottom();
    pb.move_to({l + f.top_left, t});
    pb.line_to({rr - f.top_right, t});
    if (f.top_right > 0) pb.cubic_to({rr - f.top_right + k * f.top_right, t}, {rr, t + f.top_right - k * f.top_right}, {rr, t + f.top_right});
    pb.line_to({rr, b - f.bottom_right});
    if (f.bottom_right > 0) pb.cubic_to({rr, b - f.bottom_right + k * f.bottom_right}, {rr - f.bottom_right + k * f.bottom_right, b}, {rr - f.bottom_right, b});
    pb.line_to({l + f.bottom_left, b});
    if (f.bottom_left > 0) pb.cubic_to({l + f.bottom_left - k * f.bottom_left, b}, {l, b - f.bottom_left + k * f.bottom_left}, {l, b - f.bottom_left});
    pb.line_to({l, t + f.top_left});
    if (f.top_left > 0) pb.cubic_to({l, t + f.top_left - k * f.top_left}, {l + f.top_left - k * f.top_left, t}, {l + f.top_left, t});
    pb.close();
}

// Exact analytic coverage of an axis-aligned rectangle (the fast path of UI
// backgrounds): per-column × per-row coverage of the fractional edges.
void rect_coverage(const Rect& r, PixelRect clip, Coverage& out) {
    out.values.clear();
    out.bounds = {};
    auto sub = [](float v) { return static_cast<std::int64_t>(std::floor(static_cast<double>(v) * 256.0 + 0.5)); };
    std::int64_t l = sub(r.x), t = sub(r.y), rr = sub(r.right()), b = sub(r.bottom());
    if (rr <= l || b <= t) return;
    std::int32_t x0 = static_cast<std::int32_t>(l >> 8), y0 = static_cast<std::int32_t>(t >> 8);
    std::int32_t x1 = static_cast<std::int32_t>((rr + 255) >> 8), y1 = static_cast<std::int32_t>((b + 255) >> 8);
    PixelRect box = intersect({x0, y0, x1 - x0, y1 - y0}, clip);
    if (box.empty()) return;
    auto span = [](std::int64_t lo, std::int64_t hi, std::int32_t px) {
        std::int64_t a = std::max<std::int64_t>(lo, static_cast<std::int64_t>(px) * 256);
        std::int64_t c = std::min<std::int64_t>(hi, static_cast<std::int64_t>(px + 1) * 256);
        return static_cast<std::uint32_t>(std::max<std::int64_t>(0, c - a));  // 0..256
    };
    out.bounds = box;
    out.values.resize(static_cast<std::size_t>(box.width) * box.height);
    std::vector<std::uint32_t> cols(static_cast<std::size_t>(box.width));
    for (std::int32_t x = 0; x < box.width; ++x) cols[static_cast<std::size_t>(x)] = span(l, rr, box.x + x);
    for (std::int32_t y = 0; y < box.height; ++y) {
        std::uint32_t row = span(t, b, box.y + y);
        std::uint8_t* dst = out.values.data() + static_cast<std::size_t>(y) * box.width;
        for (std::int32_t x = 0; x < box.width; ++x) {
            std::uint32_t c = (cols[static_cast<std::size_t>(x)] * row + 128) >> 8;  // 0..256
            dst[x] = static_cast<std::uint8_t>(c > 255 ? 255 : c);
        }
    }
}

std::uint8_t to8(float v) { return static_cast<std::uint8_t>(static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); }

// W3C Compositing and Blending §10: separable blend functions on
// unpremultiplied colour channels.
float blend_channel(Blend mode, float cb, float cs) {
    switch (mode) {
        case Blend::multiply: return cb * cs;
        case Blend::screen: return cb + cs - cb * cs;
        case Blend::overlay: return blend_channel(Blend::hard_light, cs, cb);
        case Blend::darken: return std::min(cb, cs);
        case Blend::lighten: return std::max(cb, cs);
        case Blend::color_dodge:
            if (cb == 0) return 0;
            if (cs == 1) return 1;
            return std::min(1.0f, cb / (1 - cs));
        case Blend::color_burn:
            if (cb == 1) return 1;
            if (cs == 0) return 0;
            return 1 - std::min(1.0f, (1 - cb) / cs);
        case Blend::hard_light:
            return cs <= 0.5f ? cb * 2 * cs : blend_channel(Blend::screen, cb, 2 * cs - 1);
        case Blend::soft_light: {
            if (cs <= 0.5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
            float d = cb <= 0.25f ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
            return cb + (2 * cs - 1) * (d - cb);
        }
        case Blend::difference: return std::abs(cb - cs);
        case Blend::exclusion: return cb + cs - 2 * cb * cs;
        default: return cs;
    }
}

Rgba8 composite_pixel(Rgba8 d, Rgba8 s, Blend mode) {
    switch (mode) {
        case Blend::normal: {
            std::uint32_t inv = 255u - s.a;
            return {static_cast<std::uint8_t>(s.r + mul255(d.r, inv)), static_cast<std::uint8_t>(s.g + mul255(d.g, inv)),
                    static_cast<std::uint8_t>(s.b + mul255(d.b, inv)), static_cast<std::uint8_t>(s.a + mul255(d.a, inv))};
        }
        case Blend::plus:
            return {static_cast<std::uint8_t>(std::min(255, d.r + s.r)), static_cast<std::uint8_t>(std::min(255, d.g + s.g)),
                    static_cast<std::uint8_t>(std::min(255, d.b + s.b)), static_cast<std::uint8_t>(std::min(255, d.a + s.a))};
        case Blend::erase: {
            std::uint32_t inv = 255u - s.a;  // destination-out
            return {static_cast<std::uint8_t>(mul255(d.r, inv)), static_cast<std::uint8_t>(mul255(d.g, inv)),
                    static_cast<std::uint8_t>(mul255(d.b, inv)), static_cast<std::uint8_t>(mul255(d.a, inv))};
        }
        default: {
            if (s.a == 0) return d;
            float sa = s.a / 255.0f, da = d.a / 255.0f;
            auto channel = [&](std::uint8_t sc, std::uint8_t dc) {
                float cs = sc / 255.0f / sa;
                float cb = da > 0 ? dc / 255.0f / da : 0.0f;
                float b = blend_channel(mode, std::clamp(cb, 0.0f, 1.0f), std::clamp(cs, 0.0f, 1.0f));
                // co = cs·αs·(1 − αb) + cb·αb·(1 − αs) + αs·αb·B(cb, cs), premultiplied
                return to8((sc / 255.0f) * (1 - da) + (dc / 255.0f) * (1 - sa) + sa * da * b);
            };
            return {channel(s.r, d.r), channel(s.g, d.g), channel(s.b, d.b), to8(sa + da - sa * da)};
        }
    }
}

}  // namespace

Canvas::Canvas(Surface& target) : base_(&target) {
    clips_.push_back({{0, 0, target.width(), target.height()}, nullptr});
}

void Canvas::enable_object_ids() {
    ids_.assign(static_cast<std::size_t>(base_->width()) * base_->height(), 0);
}

void Canvas::composite(const Coverage& cov, const Shader& shader, int dx, int dy) {
    const Clip& c = clip();
    PixelRect placed{cov.bounds.x + dx, cov.bounds.y + dy, cov.bounds.width, cov.bounds.height};
    PixelRect box = intersect(placed, c.scissor);
    if (box.empty()) return;
    Surface& dst = target();
    row_.resize(static_cast<std::size_t>(box.width));
    bool ids = !ids_.empty();  // a node drawn into a layer still owns its pixels
    for (std::int32_t y = box.y; y < box.y + box.height; ++y) {
        const std::uint8_t* cv = cov.values.data() + static_cast<std::size_t>(y - placed.y) * placed.width + (box.x - placed.x);
        const std::uint8_t* mk = c.mask ? c.mask->row(y) + box.x : nullptr;
        shader.shade(box.x, y, box.width, row_.data());
        Rgba8* out = dst.row(y) + box.x;
        for (std::int32_t i = 0; i < box.width; ++i) {
            std::uint32_t k = cv[i];
            if (mk) k = mul255(k, mk[i]);
            if (k == 0) continue;
            Rgba8 s = row_[static_cast<std::size_t>(i)];
            if (k != 255)
                s = {static_cast<std::uint8_t>(mul255(s.r, k)), static_cast<std::uint8_t>(mul255(s.g, k)),
                     static_cast<std::uint8_t>(mul255(s.b, k)), static_cast<std::uint8_t>(mul255(s.a, k))};
            if (s.a == 0 && s.r == 0 && s.g == 0 && s.b == 0) continue;
            out[i] = composite_pixel(out[i], s, Blend::normal);
            if (ids && k >= 128 && object_id_) ids_[static_cast<std::size_t>(y) * base_->width() + box.x + i] = object_id_;
        }
    }
}

void Canvas::fill_path(PathData path, FillRule rule, const Paint& paint, const Affine& m) {
    polygons_.clear();
    flatten(path, m, polygons_);
    rasterizer_.rasterize(polygons_, rule, clip().scissor, coverage_);
    composite(coverage_, Shader(paint));
}

void Canvas::stroke_path(PathData path, const Stroke& stroke, const Paint& paint, const Affine& m) {
    polygons_.clear();
    stroke_outline(path, m, stroke, polygons_);
    rasterizer_.rasterize(polygons_, FillRule::nonzero, clip().scissor, coverage_);
    composite(coverage_, Shader(paint));
}

void Canvas::fill_rect(const Rect& r, const Paint& paint, const Affine& m) {
    if (m.translation_only() || (m.b == 0 && m.c == 0)) {
        rect_coverage(m.map(r), clip().scissor, coverage_);
        composite(coverage_, Shader(paint));
        return;
    }
    PathBuilder pb;
    pb.rect(r);
    fill_path(pb.data(), FillRule::nonzero, paint, m);
}

void Canvas::fill_round_rect(const Rect& r, const CornerRadii& radii, const Paint& paint, const Affine& m) {
    if (radii.zero()) return fill_rect(r, paint, m);
    PathBuilder pb;
    rounded_rect_path(pb, r, radii);
    fill_path(pb.data(), FillRule::nonzero, paint, m);
}

void Canvas::fill_coverage(const Coverage& coverage, const Paint& paint, int dx, int dy) {
    composite(coverage, Shader(paint), dx, dy);
}

void Canvas::draw_image(const ImageView& image, const Rect& source, const Rect& dest, Sampling sampling, float opacity,
                        const Affine& m) {
    if (source.empty() || dest.empty() || !image.pixels) return;
    Paint p;
    p.kind = Paint::Kind::image;
    p.image = image;
    p.sampling = sampling;
    p.color = Color::white();
    p.opacity = opacity;
    p.transform = m * Affine::translate(dest.x, dest.y) *
                  Affine::scale(dest.width / source.width, dest.height / source.height) *
                  Affine::translate(-source.x, -source.y);
    fill_rect(dest, p, m);
}

void blur(Coverage& c, float sigma) {
    if (!(sigma > 0.25f) || c.empty()) return;
    // W3C Filter Effects: d = floor(σ·3·√(2π)/4 + 0.5), three box passes.
    int d = static_cast<int>(std::floor(sigma * 3.0f * 2.5066282746310002f / 4.0f + 0.5f));
    if (d < 1) return;
    int r = d / 2;
    int grow = 3 * (r + 1);
    PixelRect nb{c.bounds.x - grow, c.bounds.y - grow, c.bounds.width + 2 * grow, c.bounds.height + 2 * grow};
    std::vector<std::uint32_t> buf(static_cast<std::size_t>(nb.width) * nb.height, 0);
    for (std::int32_t y = 0; y < c.bounds.height; ++y)
        for (std::int32_t x = 0; x < c.bounds.width; ++x)
            buf[static_cast<std::size_t>(y + grow) * nb.width + (x + grow)] = c.values[static_cast<std::size_t>(y) * c.bounds.width + x];
    std::vector<std::uint32_t> tmp(buf.size());
    auto pass = [&](bool horizontal, int lo, int hi) {
        // Box of [−lo, +hi] around each pixel; sums in integers, the mean rounded.
        int size = lo + hi + 1;
        int w = nb.width, h = nb.height;
        int lines = horizontal ? h : w, len = horizontal ? w : h;
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) -> std::uint32_t& {
                return horizontal ? buf[static_cast<std::size_t>(line) * w + i] : buf[static_cast<std::size_t>(i) * w + line];
            };
            auto out = [&](int i) -> std::uint32_t& {
                return horizontal ? tmp[static_cast<std::size_t>(line) * w + i] : tmp[static_cast<std::size_t>(i) * w + line];
            };
            std::uint32_t sum = 0;
            for (int i = -lo; i <= hi; ++i)
                if (i >= 0 && i < len) sum += at(i);
            for (int i = 0; i < len; ++i) {
                out(i) = (sum + static_cast<std::uint32_t>(size / 2)) / static_cast<std::uint32_t>(size);
                int add = i + hi + 1, sub = i - lo;
                if (add < len) sum += at(add);
                if (sub >= 0) sum -= at(sub);
            }
        }
        buf.swap(tmp);
    };
    // An odd d: three centred boxes; an even d: two offset boxes and one of d + 1 (W3C).
    for (bool horizontal : {true, false}) {
        if (d % 2 == 1) {
            for (int i = 0; i < 3; ++i) pass(horizontal, r, r);
        } else {
            pass(horizontal, r, r - 1);
            pass(horizontal, r - 1, r);
            pass(horizontal, r, r);
        }
    }
    c.bounds = nb;
    c.values.resize(buf.size());
    for (std::size_t i = 0; i < buf.size(); ++i) c.values[i] = static_cast<std::uint8_t>(std::min<std::uint32_t>(255, buf[i]));
}

void Canvas::box_shadow(const Rect& r, const CornerRadii& radii, Point offset, float blur_radius, float spread,
                        Color color, bool inset, const Affine& m) {
    float sigma = blur_radius / 2;
    PathBuilder pb;
    Coverage shape;
    PixelRect everywhere{-(1 << 20), -(1 << 20), 1 << 21, 1 << 21};
    if (!inset) {
        Rect s{r.x + offset.x - spread, r.y + offset.y - spread, r.width + 2 * spread, r.height + 2 * spread};
        CornerRadii rr{std::max(0.0f, radii.top_left + spread), std::max(0.0f, radii.top_right + spread),
                       std::max(0.0f, radii.bottom_right + spread), std::max(0.0f, radii.bottom_left + spread)};
        rounded_rect_path(pb, s, rr);
        // Rasterise beyond the clip by the blur's reach, so the blur sees the whole edge.
        int reach = 8 + static_cast<int>(std::ceil(3 * blur_radius * m.max_scale()));
        PixelRect sc = clip().scissor;
        PixelRect limit = intersect(everywhere, {sc.x - reach, sc.y - reach, sc.width + 2 * reach, sc.height + 2 * reach});
        polygons_.clear();
        flatten(pb.data(), m, polygons_);
        rasterizer_.rasterize(polygons_, FillRule::nonzero, limit, shape);
        blur(shape, sigma * m.max_scale());
        if (shape.empty()) return;
        // The shadow shows outside the box only (CSS): shadow × (1 − box).
        PathBuilder box;
        rounded_rect_path(box, r, radii);
        Coverage inside;
        polygons_.clear();
        flatten(box.data(), m, polygons_);
        rasterizer_.rasterize(polygons_, FillRule::nonzero, shape.bounds, inside);
        if (!inside.empty())
            for (std::int32_t y = inside.bounds.y; y < inside.bounds.y + inside.bounds.height; ++y)
                for (std::int32_t x = inside.bounds.x; x < inside.bounds.x + inside.bounds.width; ++x) {
                    std::size_t i = static_cast<std::size_t>(y - shape.bounds.y) * shape.bounds.width + (x - shape.bounds.x);
                    shape.values[i] = static_cast<std::uint8_t>(mul255(shape.values[i], 255u - inside.at(x, y)));
                }
        composite(shape, Shader(Paint::solid(color)));
    } else {
        // Inset: the blurred complement of the shrunk, offset box, inside the box.
        Rect s{r.x + offset.x + spread, r.y + offset.y + spread, std::max(0.0f, r.width - 2 * spread),
               std::max(0.0f, r.height - 2 * spread)};
        CornerRadii rr{std::max(0.0f, radii.top_left - spread), std::max(0.0f, radii.top_right - spread),
                       std::max(0.0f, radii.bottom_right - spread), std::max(0.0f, radii.bottom_left - spread)};
        PathBuilder box;
        rounded_rect_path(box, r, radii);
        Coverage outer;
        polygons_.clear();
        flatten(box.data(), m, polygons_);
        rasterizer_.rasterize(polygons_, FillRule::nonzero, clip().scissor, outer);
        if (outer.empty()) return;
        PixelRect pad{outer.bounds.x - 4 - static_cast<int>(3 * blur_radius), outer.bounds.y - 4 - static_cast<int>(3 * blur_radius),
                      outer.bounds.width + 8 + static_cast<int>(6 * blur_radius), outer.bounds.height + 8 + static_cast<int>(6 * blur_radius)};
        rounded_rect_path(pb, s, rr);
        polygons_.clear();
        flatten(pb.data(), m, polygons_);
        rasterizer_.rasterize(polygons_, FillRule::nonzero, pad, shape);
        // Complement within the padded box, then blur.
        Coverage hole;
        hole.bounds = pad;
        hole.values.assign(static_cast<std::size_t>(pad.width) * pad.height, 255);
        for (std::int32_t y = 0; y < shape.bounds.height; ++y)
            for (std::int32_t x = 0; x < shape.bounds.width; ++x) {
                std::size_t i = static_cast<std::size_t>(y + shape.bounds.y - pad.y) * pad.width + (x + shape.bounds.x - pad.x);
                hole.values[i] = static_cast<std::uint8_t>(255 - shape.values[static_cast<std::size_t>(y) * shape.bounds.width + x]);
            }
        blur(hole, sigma * m.max_scale());
        Coverage out = outer;
        for (std::int32_t y = 0; y < outer.bounds.height; ++y)
            for (std::int32_t x = 0; x < outer.bounds.width; ++x) {
                int gx = x + outer.bounds.x, gy = y + outer.bounds.y;
                std::uint32_t h = 0;
                if (gx >= hole.bounds.x && gy >= hole.bounds.y && gx < hole.bounds.x + hole.bounds.width &&
                    gy < hole.bounds.y + hole.bounds.height)
                    h = hole.at(gx, gy);
                std::size_t i = static_cast<std::size_t>(y) * outer.bounds.width + x;
                out.values[i] = static_cast<std::uint8_t>(mul255(outer.values[i], h));
            }
        composite(out, Shader(Paint::solid(color)));
    }
}

void Canvas::push_mask(const Coverage& coverage) {
    const Clip& below = clip();
    auto mask = std::make_shared<Mask>(base_->width(), base_->height(), 0);
    PixelRect box = intersect(coverage.bounds, below.scissor);
    for (std::int32_t y = box.y; y < box.y + box.height; ++y) {
        std::uint8_t* row = mask->row(y);
        const std::uint8_t* b = below.mask ? below.mask->row(y) : nullptr;
        for (std::int32_t x = box.x; x < box.x + box.width; ++x) {
            std::uint32_t v = coverage.at(x, y);
            row[x] = static_cast<std::uint8_t>(b ? mul255(v, b[x]) : v);
        }
    }
    clips_.push_back({box, std::move(mask)});
}

void Canvas::push_clip_rect(const Rect& r, const Affine& m) {
    Rect d = m.map(r);
    bool axis_aligned = m.b == 0 && m.c == 0;
    // A rectangle on pixel edges is a scissor; anything else becomes a mask.
    bool on_pixels = d.x == std::floor(d.x) && d.y == std::floor(d.y) && d.right() == std::floor(d.right()) &&
                     d.bottom() == std::floor(d.bottom());
    if (axis_aligned && on_pixels) {
        PixelRect s{static_cast<std::int32_t>(d.x), static_cast<std::int32_t>(d.y), static_cast<std::int32_t>(d.width),
                    static_cast<std::int32_t>(d.height)};
        clips_.push_back({intersect(clip().scissor, s), clip().mask});
        return;
    }
    if (axis_aligned) {
        Coverage c;
        rect_coverage(d, clip().scissor, c);
        push_mask(c);
        return;
    }
    PathBuilder pb;
    pb.rect(r);
    push_clip_path(pb.data(), FillRule::nonzero, m);
}

void Canvas::push_clip_round_rect(const Rect& r, const CornerRadii& radii, const Affine& m) {
    if (radii.zero()) return push_clip_rect(r, m);
    PathBuilder pb;
    rounded_rect_path(pb, r, radii);
    push_clip_path(pb.data(), FillRule::nonzero, m);
}

void Canvas::push_clip_path(PathData path, FillRule rule, const Affine& m) {
    Coverage c;
    polygons_.clear();
    flatten(path, m, polygons_);
    rasterizer_.rasterize(polygons_, rule, clip().scissor, c);
    if (c.empty()) {
        clips_.push_back({{0, 0, 0, 0}, nullptr});
        return;
    }
    push_mask(c);
}

void Canvas::pop_clip() {
    if (clips_.size() > 1) clips_.pop_back();
}

void Canvas::push_layer(float opacity, Blend blend) {
    auto surface = std::make_unique<Surface>(base_->width(), base_->height());
    layers_.push_back({std::move(surface), std::clamp(opacity, 0.0f, 1.0f), blend, clips_.size()});
}

void Canvas::pop_layer() {
    if (layers_.empty()) return;
    Layer layer = std::move(layers_.back());
    layers_.pop_back();
    Surface& dst = target();
    const Clip& c = clip();
    std::uint32_t op = static_cast<std::uint32_t>(layer.opacity * 255.0f + 0.5f);
    PixelRect box = c.scissor;
    for (std::int32_t y = box.y; y < box.y + box.height; ++y) {
        const Rgba8* src = layer.surface->row(y);
        Rgba8* out = dst.row(y);
        const std::uint8_t* mk = c.mask ? c.mask->row(y) : nullptr;
        for (std::int32_t x = box.x; x < box.x + box.width; ++x) {
            Rgba8 s = src[x];
            if (s.a == 0 && s.r == 0 && s.g == 0 && s.b == 0) continue;
            std::uint32_t k = mk ? mul255(op, mk[x]) : op;
            if (k != 255)
                s = {static_cast<std::uint8_t>(mul255(s.r, k)), static_cast<std::uint8_t>(mul255(s.g, k)),
                     static_cast<std::uint8_t>(mul255(s.b, k)), static_cast<std::uint8_t>(mul255(s.a, k))};
            out[x] = composite_pixel(out[x], s, layer.blend);
        }
    }
}

}  // namespace omui::raster
