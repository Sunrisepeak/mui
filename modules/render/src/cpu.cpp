module openmui.render;

import std;
import openmui.raster;

namespace omui::render {

namespace {

static_assert(static_cast<int>(PathVerb::close) == static_cast<int>(raster::Verb::close));
static_assert(static_cast<int>(BlendMode::erase) == static_cast<int>(raster::Blend::erase),
              "render and raster list the blend modes in the same order");

struct PathConv {
    std::vector<raster::Verb> verbs;
    raster::PathData data(const PathView& v) {
        verbs.resize(v.verbs.size());
        for (std::size_t i = 0; i < v.verbs.size(); ++i) verbs[i] = static_cast<raster::Verb>(v.verbs[i]);
        return {verbs, v.points};
    }
};

struct PaintConv {
    std::vector<raster::Stop> stops;
    raster::Paint convert(const Paint& p, const Resources& res, RenderReport& report) {
        raster::Paint out;
        switch (p.kind) {
            case Paint::Kind::solid:
                out = raster::Paint::solid(p.color);
                break;
            case Paint::Kind::gradient: {
                out.kind = raster::Paint::Kind::gradient;
                out.gradient = static_cast<raster::GradientKind>(p.gradient.kind);
                out.start = p.gradient.start;
                out.end = p.gradient.end;
                out.radius = p.gradient.radius;
                out.spread = static_cast<raster::Spread>(p.gradient.spread);
                stops.clear();
                for (const GradientStop& s : p.gradient.stops) stops.push_back({s.offset, s.color});
                out.stops = stops;
                break;
            }
            case Paint::Kind::image: {
                const raster::Surface* img = res.images ? res.images->get(p.image) : nullptr;
                if (!img) {
                    ++report.missing_images;
                    out = raster::Paint::solid(Color::transparent());
                    break;
                }
                out.kind = raster::Paint::Kind::image;
                out.image = img->view();
                out.color = p.color;
                out.sampling = static_cast<raster::Sampling>(p.sampling);
                out.transform = p.image_transform;
                break;
            }
        }
        return out;
    }
};

raster::Stroke stroke_of(const StrokeStyle& s) {
    return {.width = s.width,
            .cap = static_cast<raster::Cap>(s.cap),
            .join = static_cast<raster::Join>(s.join),
            .miter_limit = s.miter_limit,
            .dashes = s.dashes,
            .dash_offset = s.dash_offset};
}

}  // namespace

RenderReport CpuRenderer::render(const DisplayList& list, raster::Surface& target, const Resources& res,
                                 const CpuOptions& options) {
    RenderReport report;
    if (options.clear) target.clear(raster::premultiply(options.background));
    raster::Canvas canvas(target);
    if (options.object_ids) canvas.enable_object_ids();

    std::vector<Affine> transforms{Affine::scale(options.scale, options.scale)};
    std::vector<std::uint32_t> nodes{0};
    PathConv paths;
    PaintConv paints;

    auto m = [&]() -> const Affine& { return transforms.back(); };
    // The paint's own transform is in logical space; the device needs it after the current matrix.
    auto device_paint = [&](const Paint& p) {
        raster::Paint rp = paints.convert(p, res, report);
        if (rp.kind != raster::Paint::Kind::solid) rp.transform = m() * rp.transform;
        return rp;
    };

    for (const Command& command : list.commands()) {
        ++report.commands;
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::same_as<T, FillRect>) {
                    canvas.fill_rect(c.rect, device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, FillRoundRect>) {
                    canvas.fill_round_rect(c.rect, c.radii, device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                    Path p;
                    p.add_rounded_rect(c.rect, c.radii);
                    canvas.stroke_path(paths.data(p.view()), {.width = c.width}, device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, BoxShadow>) {
                    canvas.box_shadow(c.rect, c.radii, c.offset, c.blur, c.spread, c.color, c.inset, m());
                } else if constexpr (std::same_as<T, FillPath>) {
                    canvas.fill_path(paths.data(c.path), static_cast<raster::FillRule>(c.rule), device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, StrokePath>) {
                    canvas.stroke_path(paths.data(c.path), stroke_of(c.style), device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, DrawImage>) {
                    const raster::Surface* img = res.images ? res.images->get(c.image) : nullptr;
                    if (!img) {
                        ++report.missing_images;
                        return;
                    }
                    canvas.draw_image(img->view(), c.source, c.dest, static_cast<raster::Sampling>(c.sampling), c.opacity, m());
                } else if constexpr (std::same_as<T, DrawGlyphs>) {
                    if (!res.glyphs) {
                        report.missing_glyphs += c.glyphs.size();
                        return;
                    }
                    raster::Paint paint = device_paint(c.paint);
                    const Affine& mm = m();
                    bool simple = mm.b == 0 && mm.c == 0 && mm.a == mm.d && mm.a > 0;
                    for (const Glyph& g : c.glyphs) {
                        Point pen{c.origin.x + g.position.x, c.origin.y + g.position.y};
                        if (simple) {
                            Point d = mm.apply(pen);
                            float ix = std::floor(d.x), iy = std::floor(d.y);
                            GlyphMask gm = res.glyphs->mask(c.font, g.id, c.size * mm.a, {d.x - ix, d.y - iy});
                            if (!gm.coverage) {
                                ++report.missing_glyphs;
                                continue;
                            }
                            canvas.fill_coverage(*gm.coverage, paint, static_cast<int>(ix), static_cast<int>(iy));
                        } else {
                            raster::PathData outline = res.glyphs->outline(c.font, g.id);
                            canvas.fill_path(outline, raster::FillRule::nonzero, paint,
                                             mm * Affine::translate(pen.x, pen.y) * Affine::scale(c.size, c.size));
                        }
                    }
                } else if constexpr (std::same_as<T, PushClipRect>) {
                    canvas.push_clip_round_rect(c.rect, c.radii, m());
                } else if constexpr (std::same_as<T, PushClipPath>) {
                    canvas.push_clip_path(paths.data(c.path), static_cast<raster::FillRule>(c.rule), m());
                } else if constexpr (std::same_as<T, PopClip>) {
                    canvas.pop_clip();
                } else if constexpr (std::same_as<T, PushTransform>) {
                    transforms.push_back(m() * c.transform);
                } else if constexpr (std::same_as<T, PopTransform>) {
                    if (transforms.size() > 1) transforms.pop_back();
                } else if constexpr (std::same_as<T, PushLayer>) {
                    if (c.effect.id != 0) ++report.effects_skipped;
                    canvas.push_layer(c.opacity, static_cast<raster::Blend>(c.blend));
                } else if constexpr (std::same_as<T, PopLayer>) {
                    canvas.pop_layer();
                } else if constexpr (std::same_as<T, ExternalTexture>) {
                    ++report.external_skipped;
                } else if constexpr (std::same_as<T, BeginNode>) {
                    nodes.push_back(c.node);
                    canvas.set_object_id(c.node);
                } else if constexpr (std::same_as<T, EndNode>) {
                    if (nodes.size() > 1) nodes.pop_back();
                    canvas.set_object_id(nodes.back());
                }
            },
            command);
    }
    if (options.object_ids) ids_.assign(canvas.object_ids().begin(), canvas.object_ids().end());
    else ids_.clear();
    return report;
}

}  // namespace omui::render
