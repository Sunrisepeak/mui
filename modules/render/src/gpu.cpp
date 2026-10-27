module openmui.render;

import std;
import openmui.geom;
import openmui.raster;
import openmui.rhi;
import :shared;

namespace omui::render {

namespace {

namespace ui = shaders::ui;

// One instance of the shader's quad (shaders/ui.slang, struct Prim).
struct Prim {
    std::array<float, 4> rect{}, m0{}, m1{}, radii{}, shape{}, color{}, paint{}, pgeo{}, pinv0{}, pinv1{}, clip{}, uv{};
};
static_assert(sizeof(Prim) == 12 * 16);

constexpr float kind_rect = 0, kind_rrect = 1, kind_mask = 2, kind_image = 3;
constexpr int flag_axis = 1, flag_clip_mask = 2;

struct Draw {
    Prim         prim;
    rhi::Texture image;  // the image slot's texture (none: white)
    bool         linear = false;
    rhi::Texture mask;   // the clip mask (none: white)
};

// A run of draws into one target, the passes in the order they run: a
// layer's pass runs before the rest of its parent's.
struct Segment {
    rhi::Texture         target;
    rhi::LoadOp          load = rhi::LoadOp::load;
    std::array<float, 4> clear{};
    std::vector<Draw>    draws;
};

struct Frame {
    std::array<float, 4> size;   // target w, h; atlas w, h
    std::array<float, 4> ramps;  // ramp rows
};

struct Clip {
    float        x0, y0, x1, y1;  // the device rectangle
    rhi::Texture mask;           // the mask texture, or none
    std::shared_ptr<std::vector<std::uint8_t>> coverage;  // the mask, target-sized
};

std::array<float, 4> premultiplied(Color c) { return {c.r * c.a, c.g * c.a, c.b * c.a, c.a}; }

void set(std::array<float, 4>& v, float a, float b, float c, float d) { v = {a, b, c, d}; }

void transform(Prim& p, const Affine& m, float kind, int flags) {
    set(p.m0, m.a, m.b, m.c, m.d);
    set(p.m1, m.e, m.f, kind, static_cast<float>(flags));
}

void inverse(Prim& p, const Affine& m) {
    const Affine inv = m.inverse().value_or(Affine{});
    set(p.pinv0, inv.a, inv.b, inv.c, inv.d);
    p.pinv1[0] = inv.e;
    p.pinv1[1] = inv.f;
}

raster::PixelRect pixels(const Clip& c) {
    const auto x0 = static_cast<std::int32_t>(std::floor(c.x0)), y0 = static_cast<std::int32_t>(std::floor(c.y0));
    const auto x1 = static_cast<std::int32_t>(std::ceil(c.x1)), y1 = static_cast<std::int32_t>(std::ceil(c.y1));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

}  // namespace

struct GpuRenderer::Impl {
    rhi::Device&          d;
    rhi::GraphicsPipeline pipeline;
    rhi::Sampler          nearest, linear;
    rhi::Texture          white, white_mask;
    rhi::Texture          ramps;
    std::uint32_t         ramp_capacity = 0;
    rhi::Texture          atlas;
    std::uint32_t         atlas_w = 2048, atlas_h = 0;
    rhi::Buffer           instances;
    std::uint32_t         instance_capacity = 0;
    std::unordered_map<const raster::Surface*, rhi::Texture> images;
    std::vector<rhi::Texture> layers, masks;
    std::uint32_t             layer_w = 0, layer_h = 0;

    std::array<rhi::VertexAttribute, 12> attributes{};
    std::array<rhi::VertexBufferLayout, 1> layout{};

    // The frame being built.
    std::uint32_t                       width = 0, height = 0;
    std::vector<Segment>                segments;
    std::vector<std::array<raster::Rgba8, 256>> ramp_rows;
    struct AtlasEntry {
        std::uint32_t x, y;
        const raster::Coverage* coverage;
    };
    std::vector<AtlasEntry>             atlas_entries;
    std::deque<raster::Coverage>        owned;  // the frame's coverage (a deque: the atlas list points into it)
    std::uint32_t                       shelf_x = 0, shelf_y = 0, shelf_h = 0;
    std::vector<const raster::Surface*> images_used;
    std::size_t                         layers_used = 0, masks_used = 0;
    rhi::CommandList                    list;
    raster::Polygons                    polygons;
    raster::Rasterizer                  rasterizer;

    explicit Impl(rhi::Device& device) : d(device) {
        for (std::uint32_t i = 0; i < 12; ++i) attributes[i] = {i, rhi::VertexFormat::float4, i * 16};
        layout[0] = {sizeof(Prim), true, attributes};
    }
    ~Impl() {
        static_cast<void>(d.wait_idle());
        for (rhi::Texture t : layers) d.destroy(t);
        for (rhi::Texture t : masks) d.destroy(t);
        for (auto& [s, t] : images) d.destroy(t);
        for (rhi::Texture t : {white, white_mask, ramps, atlas}) d.destroy(t);
        d.destroy(instances);
        d.destroy(nearest);
        d.destroy(linear);
        d.destroy(pipeline);
    }

    rhi::Result<void> init() {
        auto p = d.create(rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &ui::ui_fs, .vertex_buffers = layout,
                                                    .blend = rhi::Blend::premultiplied(), .label = "render.ui"});
        if (!p) return std::unexpected(p.error());
        pipeline = *p;
        auto n = d.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
        auto l = d.create(rhi::SamplerDesc{rhi::Filter::linear, rhi::Filter::linear});
        if (!n || !l) return std::unexpected(n ? l.error() : n.error());
        nearest = *n;
        linear = *l;
        auto w = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 1, 1, rhi::TextureUsage::sampled, "render.white"});
        auto wm = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, 1, 1, rhi::TextureUsage::sampled, "render.white_mask"});
        if (!w || !wm) return std::unexpected(w ? wm.error() : w.error());
        white = *w;
        white_mask = *wm;
        rhi::CommandList up;
        const std::array<std::uint8_t, 4> one{255, 255, 255, 255};
        up.upload(white, rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one)));
        up.upload(white_mask, rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one).first(1)));
        auto s = d.submit(up);
        if (!s) return std::unexpected(s.error());
        return d.wait(*s);
    }

    // ── per-frame resources ──────────────────────────────────────────────

    rhi::Result<void> ensure_ramps(std::uint32_t rows) {
        if (rows <= ramp_capacity && ramps) return {};
        std::uint32_t cap = std::max<std::uint32_t>(16, ramp_capacity);
        while (cap < rows) cap *= 2;
        if (ramps) d.destroy(ramps);
        auto t = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 256, cap, rhi::TextureUsage::sampled, "render.ramps"});
        if (!t) return std::unexpected(t.error());
        ramps = *t;
        ramp_capacity = cap;
        return {};
    }

    rhi::Result<void> ensure_atlas(std::uint32_t h) {
        h = std::max<std::uint32_t>(h, 1);
        if (atlas && h <= atlas_h) return {};
        std::uint32_t cap = std::max<std::uint32_t>(256, atlas_h);
        while (cap < h) cap *= 2;
        if (cap > d.caps().max_texture_size) return rhi::fail(rhi::Errc::out_of_memory, "the frame's coverage does not fit the atlas");
        if (atlas) d.destroy(atlas);
        auto t = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, atlas_w, cap, rhi::TextureUsage::sampled, "render.atlas"});
        if (!t) return std::unexpected(t.error());
        atlas = *t;
        atlas_h = cap;
        return {};
    }

    rhi::Result<rhi::Texture> layer_texture() {
        if (layers_used < layers.size()) return layers[layers_used++];
        auto t = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, width, height,
                                           rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "render.layer"});
        if (!t) return std::unexpected(t.error());
        layers.push_back(*t);
        ++layers_used;
        return *t;
    }

    rhi::Result<rhi::Texture> mask_texture() {
        if (masks_used < masks.size()) return masks[masks_used++];
        auto t = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, width, height, rhi::TextureUsage::sampled, "render.clip_mask"});
        if (!t) return std::unexpected(t.error());
        masks.push_back(*t);
        ++masks_used;
        return *t;
    }

    rhi::Result<rhi::Texture> image_texture(const raster::Surface& s) {
        auto it = images.find(&s);
        if (it != images.end()) {
            const auto desc = d.describe(it->second);
            if (!desc || desc->width != static_cast<std::uint32_t>(s.width()) || desc->height != static_cast<std::uint32_t>(s.height())) {
                d.destroy(it->second);
                images.erase(it);
                it = images.end();
            }
        }
        if (it == images.end()) {
            auto t = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, static_cast<std::uint32_t>(s.width()),
                                               static_cast<std::uint32_t>(s.height()), rhi::TextureUsage::sampled, "render.image"});
            if (!t) return std::unexpected(t.error());
            it = images.emplace(&s, *t).first;
        }
        // Uploaded again each frame it is drawn in: a surface's pixels may have changed.
        if (std::ranges::find(images_used, &s) == images_used.end()) {
            images_used.push_back(&s);
            for (int y = 0; y < s.height(); ++y)
                list.upload(it->second, rhi::Region{0, static_cast<std::uint32_t>(y), static_cast<std::uint32_t>(s.width()), 1},
                            std::as_bytes(std::span(s.row(y), static_cast<std::size_t>(s.width()))));
        }
        return it->second;
    }

    // A place in the atlas for `c` (its bounds' size): shelves 2048 wide.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> place(const raster::Coverage& c) {
        const auto w = static_cast<std::uint32_t>(c.bounds.width), h = static_cast<std::uint32_t>(c.bounds.height);
        if (w == 0 || h == 0 || w > atlas_w) return std::nullopt;
        if (shelf_x + w > atlas_w) {
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        const std::pair<std::uint32_t, std::uint32_t> at{shelf_x, shelf_y};
        shelf_x += w + 1;  // a texel apart: nearest sampling never bleeds, but keep the seam
        shelf_h = std::max(shelf_h, h + 1);
        atlas_entries.push_back({at.first, at.second, &c});
        return at;
    }
};

GpuRenderer::GpuRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuRenderer::GpuRenderer(GpuRenderer&&) noexcept = default;
GpuRenderer& GpuRenderer::operator=(GpuRenderer&&) noexcept = default;
GpuRenderer::~GpuRenderer() = default;

rhi::Device& GpuRenderer::device() const noexcept { return impl_->d; }

rhi::ShaderFormats gpu_shader_formats() noexcept { return ui::ui_vs.formats(); }

rhi::Result<GpuRenderer> GpuRenderer::create(rhi::Device& device) {
    auto impl = std::make_unique<Impl>(device);
    if (auto r = impl->init(); !r) return std::unexpected(r.error());
    return GpuRenderer(std::move(impl));
}

rhi::Result<RenderReport> GpuRenderer::render(const DisplayList& dl, rhi::Texture target, const Resources& res, const GpuOptions& options) {
    Impl& g = *impl_;
    const auto desc = g.d.describe(target);
    if (!desc) return rhi::fail(rhi::Errc::invalid, "render: a stale target");
    if (desc->format != rhi::Format::rgba8_unorm || !rhi::has(desc->usage, rhi::TextureUsage::color_target))
        return rhi::fail(rhi::Errc::invalid, "render: the target must be an rgba8_unorm colour target");
    if (g.width != desc->width || g.height != desc->height) {
        static_cast<void>(g.d.wait_idle());
        for (rhi::Texture t : g.layers) g.d.destroy(t);
        for (rhi::Texture t : g.masks) g.d.destroy(t);
        g.layers.clear();
        g.masks.clear();
    }
    g.width = desc->width;
    g.height = desc->height;
    const auto W = static_cast<float>(g.width), H = static_cast<float>(g.height);

    RenderReport report;
    g.segments.clear();
    g.ramp_rows.clear();
    g.atlas_entries.clear();
    g.owned.clear();
    g.shelf_x = g.shelf_y = g.shelf_h = 0;
    g.images_used.clear();
    g.layers_used = g.masks_used = 0;
    g.list.clear();
    rhi::Error failure{};
    bool failed = false;
    auto fail = [&](const rhi::Error& e) {
        if (!failed) failure = e, failed = true;
    };

    g.segments.push_back({target, options.clear ? rhi::LoadOp::clear : rhi::LoadOp::load, premultiplied(options.background), {}});
    std::size_t current = 0;
    std::vector<Affine> transforms{Affine::scale(options.scale, options.scale)};
    std::vector<Clip> clips{{0, 0, W, H, {}, nullptr}};
    struct Open {
        std::size_t  parent;
        rhi::Texture texture;
        float        opacity;
    };
    std::vector<Open> open;
    PathConv paths;
    std::vector<raster::Stop> stops;
    Path scratch_path;

    auto m = [&]() -> const Affine& { return transforms.back(); };
    auto clip_into = [&](Prim& p, Draw& d, int& flags) {
        const Clip& c = clips.back();
        set(p.clip, c.x0, c.y0, c.x1, c.y1);
        if (c.mask) {
            flags |= flag_clip_mask;
            d.mask = c.mask;
        }
    };
    // The paint's colour fields; false when it cannot be drawn (a missing image).
    auto paint_into = [&](Prim& p, Draw& d, const Paint& paint) -> bool {
        p.shape[1] = 1;
        switch (paint.kind) {
            case Paint::Kind::solid:
                p.color = premultiplied(paint.color);
                p.paint = {0, 0, 0, 0};
                return true;
            case Paint::Kind::gradient: {
                raster::Paint rp;
                rp.kind = raster::Paint::Kind::gradient;
                rp.gradient = static_cast<raster::GradientKind>(paint.gradient.kind);
                rp.start = paint.gradient.start;
                rp.end = paint.gradient.end;
                rp.radius = paint.gradient.radius;
                rp.spread = static_cast<raster::Spread>(paint.gradient.spread);
                stops.clear();
                for (const GradientStop& s : paint.gradient.stops) stops.push_back({s.offset, s.color});
                rp.stops = stops;
                const raster::Shader shader(rp);
                std::array<raster::Rgba8, 256> row{};
                std::ranges::copy(shader.table(), row.begin());
                g.ramp_rows.push_back(row);
                const float kind = paint.gradient.kind == GradientKind::linear ? 1.0f : paint.gradient.kind == GradientKind::radial ? 2.0f : 3.0f;
                set(p.paint, kind, static_cast<float>(g.ramp_rows.size() - 1), static_cast<float>(paint.gradient.spread), paint.gradient.radius);
                if (paint.gradient.kind == GradientKind::radial)
                    set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.radius, 0);
                else
                    set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.end.x, paint.gradient.end.y);
                inverse(p, m());
                return true;
            }
            case Paint::Kind::image: {
                const raster::Surface* img = res.images ? res.images->get(paint.image) : nullptr;
                if (!img) {
                    ++report.missing_images;
                    return false;
                }
                auto t = g.image_texture(*img);
                if (!t) {
                    fail(t.error());
                    return false;
                }
                d.image = *t;
                d.linear = paint.sampling == Sampling::linear;
                p.color = {0, 0, 0, paint.color.a};
                p.paint = {4, 0, 0, 0};
                inverse(p, m() * paint.image_transform);
                p.pinv1[2] = static_cast<float>(img->width());
                p.pinv1[3] = static_cast<float>(img->height());
                return true;
            }
        }
        return false;
    };
    auto emit = [&](Draw d) { g.segments[current].draws.push_back(std::move(d)); };
    // An analytic rectangle or rounded rectangle.
    auto shape = [&](const Rect& r, const CornerRadii& radii, const Paint& paint) {
        Draw d;
        Prim& p = d.prim;
        if (!paint_into(p, d, paint)) return;
        const Affine& mm = m();
        const bool rounded = !radii.zero();
        int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
        clip_into(p, d, flags);
        transform(p, mm, rounded ? kind_rrect : kind_rect, flags);
        set(p.rect, r.x, r.y, r.width, r.height);
        const CornerRadii f = radii.fitted(r.size());
        set(p.radii, f.top_left, f.top_right, f.bottom_right, f.bottom_left);
        emit(std::move(d));
    };
    // Coverage the rasteriser made, in device space, under a paint. What the
    // frame does not own (a glyph cache's mask, which a later lookup may
    // evict) is copied first.
    auto mask = [&](const raster::Coverage& from, int dx, int dy, const Paint& paint, float scale = 1, bool frame_owned = true) {
        if (from.empty()) return;
        const raster::Coverage& c = frame_owned ? from : g.owned.emplace_back(from);
        const auto at = g.place(c);
        if (!at) {
            fail({rhi::Errc::out_of_memory, std::format("a coverage mask {}×{} wider than the atlas", c.bounds.width, c.bounds.height)});
            return;
        }
        Draw d;
        Prim& p = d.prim;
        if (!paint_into(p, d, paint)) return;
        p.shape[1] = scale;
        int flags = 0;
        clip_into(p, d, flags);
        transform(p, Affine{}, kind_mask, flags);
        set(p.rect, static_cast<float>(c.bounds.x + dx), static_cast<float>(c.bounds.y + dy), static_cast<float>(c.bounds.width),
            static_cast<float>(c.bounds.height));
        set(p.uv, static_cast<float>(at->first), static_cast<float>(at->second), 0, 0);
        emit(std::move(d));
    };
    auto owned = [&]() -> raster::Coverage& { return g.owned.emplace_back(); };
    // An image's pixels over `dest` (local space under `mm`).
    auto image = [&](const raster::Surface& img, const Rect& source, const Rect& dest, Sampling sampling, float opacity, const Affine& mm) {
        auto t = g.image_texture(img);
        if (!t) {
            fail(t.error());
            return;
        }
        Draw d;
        Prim& p = d.prim;
        d.image = *t;
        d.linear = sampling == Sampling::linear;
        int flags = (mm.b == 0 && mm.c == 0) ? flag_axis : 0;
        clip_into(p, d, flags);
        transform(p, mm, kind_image, flags);
        set(p.rect, dest.x, dest.y, dest.width, dest.height);
        set(p.uv, source.x, source.y, source.width, source.height);
        p.shape[1] = opacity;
        p.pinv1[2] = static_cast<float>(img.width());
        p.pinv1[3] = static_cast<float>(img.height());
        emit(std::move(d));
    };
    auto push_mask_clip = [&](const raster::Coverage& c) {
        const Clip& below = clips.back();
        auto values = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(g.width) * g.height, 0);
        for (std::int32_t y = std::max(0, c.bounds.y); y < std::min<std::int32_t>(static_cast<std::int32_t>(g.height), c.bounds.y + c.bounds.height); ++y)
            for (std::int32_t x = std::max(0, c.bounds.x); x < std::min<std::int32_t>(static_cast<std::int32_t>(g.width), c.bounds.x + c.bounds.width); ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * g.width + static_cast<std::size_t>(x);
                const std::uint32_t under = below.coverage ? (*below.coverage)[i] : 255u;
                (*values)[i] = static_cast<std::uint8_t>(raster::mul255(c.at(x, y), under));
            }
        auto t = g.mask_texture();
        if (!t) {
            fail(t.error());
            clips.push_back(below);
            return;
        }
        g.list.upload(*t, rhi::Region{0, 0, g.width, g.height}, std::as_bytes(std::span(*values)));
        clips.push_back({below.x0, below.y0, below.x1, below.y1, *t, std::move(values)});
    };

    for (const Command& command : dl.commands()) {
        if (failed) break;
        ++report.commands;
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::same_as<T, FillRect>) {
                    shape(c.rect, {}, c.paint);
                } else if constexpr (std::same_as<T, FillRoundRect>) {
                    shape(c.rect, c.radii, c.paint);
                } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                    scratch_path.clear();
                    scratch_path.add_rounded_rect(c.rect, c.radii);
                    raster::Coverage& cov = owned();
                    raster::stroke_coverage(paths.data(scratch_path.view()), {.width = c.width}, m(), pixels(clips.back()), cov);
                    mask(cov, 0, 0, c.paint);
                } else if constexpr (std::same_as<T, BoxShadow>) {
                    raster::Coverage& cov = owned();
                    raster::box_shadow_coverage(c.rect, c.radii, c.offset, c.blur, c.spread, c.inset, m(), pixels(clips.back()), cov, g.polygons,
                                                g.rasterizer);
                    mask(cov, 0, 0, Paint::solid(c.color));
                } else if constexpr (std::same_as<T, FillPath>) {
                    raster::Coverage& cov = owned();
                    raster::fill_coverage(paths.data(c.path), m(), static_cast<raster::FillRule>(c.rule), pixels(clips.back()), cov);
                    mask(cov, 0, 0, c.paint);
                } else if constexpr (std::same_as<T, StrokePath>) {
                    raster::Coverage& cov = owned();
                    raster::stroke_coverage(paths.data(c.path),
                                            {.width = c.style.width,
                                             .cap = static_cast<raster::Cap>(c.style.cap),
                                             .join = static_cast<raster::Join>(c.style.join),
                                             .miter_limit = c.style.miter_limit,
                                             .dashes = c.style.dashes,
                                             .dash_offset = c.style.dash_offset},
                                            m(), pixels(clips.back()), cov);
                    mask(cov, 0, 0, c.paint);
                } else if constexpr (std::same_as<T, DrawImage>) {
                    const raster::Surface* img = res.images ? res.images->get(c.image) : nullptr;
                    if (!img) {
                        ++report.missing_images;
                        return;
                    }
                    image(*img, c.source, c.dest, c.sampling, c.opacity, m());
                } else if constexpr (std::same_as<T, DrawGlyphs>) {
                    if (!res.glyphs) {
                        report.missing_glyphs += c.glyphs.size();
                        return;
                    }
                    const Affine& mm = m();
                    for (std::size_t i = 0; i < c.glyphs.size(); ++i) {
                        const Glyph& gl = c.glyphs[i];
                        const Placed pl = place(c, i, mm);
                        float opacity = 1;
                        Paint paint = c.paint;
                        if (pl.style) {
                            // The glyph's own opacity and tint over the run's colour (D-68).
                            opacity = std::clamp(pl.style->opacity, 0.0f, 1.0f);
                            if (opacity <= 0) continue;
                            if (c.paint.kind == Paint::Kind::solid) {
                                const Color t = pl.style->tint;
                                const float k = std::clamp(t.a, 0.0f, 1.0f);
                                const Color col = c.paint.color;
                                paint = Paint::solid({col.r + (t.r - col.r) * k, col.g + (t.g - col.g) * k, col.b + (t.b - col.b) * k, col.a * opacity});
                            }
                        }
                        // A single glyph's layer at an opacity is its coverage scaled.
                        const float scale = pl.style && c.paint.kind != Paint::Kind::solid ? opacity : 1.0f;
                        if (pl.mask) {
                            const MaskCall call = mask_call(pl.pen, c.size, mm);
                            const GlyphMask gm = res.glyphs->mask(c.font, gl.id, call.size, call.fraction);
                            if (gm.color) {
                                const auto& b = gm.color_bounds;
                                image(*gm.color, {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                      {call.ix + b.x, call.iy + b.y, static_cast<float>(b.width), static_cast<float>(b.height)},
                                      Sampling::nearest, c.paint.color.a * opacity, Affine{});
                            } else if (gm.coverage) {
                                mask(*gm.coverage, static_cast<int>(call.ix), static_cast<int>(call.iy), paint, scale, false);
                            } else {
                                ++report.missing_glyphs;
                            }
                        } else {
                            const raster::PathData outline = res.glyphs->outline(c.font, gl.id);
                            if (!outline.verbs.empty()) {
                                raster::Coverage& cov = owned();
                                raster::fill_coverage(outline, pl.outline, raster::FillRule::nonzero, pixels(clips.back()), cov);
                                mask(cov, 0, 0, paint, scale);
                            } else {
                                // A colour glyph (emoji): its image at device size, under the glyph's transform.
                                const float s = mm.max_scale();
                                const GlyphMask gm = res.glyphs->mask(c.font, gl.id, c.size * s, {0, 0});
                                if (gm.color) {
                                    const auto& b = gm.color_bounds;
                                    const Affine to = mm * Affine::translate(pl.pen.x, pl.pen.y) * (pl.style ? pl.style->transform : Affine{}) *
                                                      Affine::scale(1 / s, 1 / s);
                                    image(*gm.color, {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                          {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.width), static_cast<float>(b.height)},
                                          Sampling::linear, c.paint.color.a * opacity, to);
                                } else {
                                    ++report.missing_glyphs;
                                }
                            }
                        }
                    }
                } else if constexpr (std::same_as<T, PushClipRect>) {
                    const Affine& mm = m();
                    if (c.radii.zero() && mm.b == 0 && mm.c == 0) {
                        const Rect r = mm.map(c.rect);
                        const Clip& below = clips.back();
                        clips.push_back({std::max(below.x0, r.x), std::max(below.y0, r.y), std::min(below.x1, r.right()), std::min(below.y1, r.bottom()),
                                         below.mask, below.coverage});
                    } else {
                        raster::PathBuilder pb;
                        raster::rounded_rect_path(pb, c.rect, c.radii);
                        raster::Coverage cov;
                        raster::fill_coverage(pb.data(), mm, raster::FillRule::nonzero, {0, 0, static_cast<std::int32_t>(g.width), static_cast<std::int32_t>(g.height)}, cov);
                        push_mask_clip(cov);
                    }
                } else if constexpr (std::same_as<T, PushClipPath>) {
                    raster::Coverage cov;
                    raster::fill_coverage(paths.data(c.path), m(), static_cast<raster::FillRule>(c.rule),
                                          {0, 0, static_cast<std::int32_t>(g.width), static_cast<std::int32_t>(g.height)}, cov);
                    push_mask_clip(cov);
                } else if constexpr (std::same_as<T, PopClip>) {
                    if (clips.size() > 1) clips.pop_back();
                } else if constexpr (std::same_as<T, PushTransform>) {
                    transforms.push_back(m() * c.transform);
                } else if constexpr (std::same_as<T, PopTransform>) {
                    if (transforms.size() > 1) transforms.pop_back();
                } else if constexpr (std::same_as<T, PushLayer>) {
                    if (c.effect.id != 0) ++report.effects_skipped;
                    if (c.blend != BlendMode::normal) ++report.blends_skipped;
                    auto t = g.layer_texture();
                    if (!t) {
                        fail(t.error());
                        return;
                    }
                    open.push_back({current, *t, c.opacity});
                    g.segments.push_back({*t, rhi::LoadOp::clear, {0, 0, 0, 0}, {}});
                    current = g.segments.size() - 1;
                } else if constexpr (std::same_as<T, PopLayer>) {
                    if (open.empty()) return;
                    const Open o = open.back();
                    open.pop_back();
                    g.segments.push_back({g.segments[o.parent].target, rhi::LoadOp::load, {}, {}});
                    current = g.segments.size() - 1;
                    // The layer over its parent, whole: what it holds was clipped as it was drawn.
                    Draw d;
                    Prim& p = d.prim;
                    d.image = o.texture;
                    transform(p, Affine{}, kind_image, flag_axis);
                    set(p.rect, 0, 0, W, H);
                    set(p.uv, 0, 0, W, H);
                    set(p.clip, 0, 0, W, H);
                    p.shape[1] = std::clamp(o.opacity, 0.0f, 1.0f);
                    p.pinv1[2] = W;
                    p.pinv1[3] = H;
                    emit(std::move(d));
                } else if constexpr (std::same_as<T, ExternalTexture>) {
                    ++report.external_skipped;
                }
            },
            command);
    }
    if (failed) return std::unexpected(failure);

    // ── what the passes read: ramps, atlas, instances ────────────────────
    if (!g.ramp_rows.empty()) {
        if (auto r = g.ensure_ramps(static_cast<std::uint32_t>(g.ramp_rows.size())); !r) return std::unexpected(r.error());
        for (std::size_t i = 0; i < g.ramp_rows.size(); ++i)
            g.list.upload(g.ramps, rhi::Region{0, static_cast<std::uint32_t>(i), 256, 1}, std::as_bytes(std::span(g.ramp_rows[i])));
    } else if (auto r = g.ensure_ramps(1); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = g.ensure_atlas(g.shelf_y + g.shelf_h); !r) return std::unexpected(r.error());
    for (const auto& e : g.atlas_entries)
        g.list.upload(g.atlas, rhi::Region{e.x, e.y, static_cast<std::uint32_t>(e.coverage->bounds.width), static_cast<std::uint32_t>(e.coverage->bounds.height)},
                      std::as_bytes(std::span(e.coverage->values)));
    std::size_t total = 0;
    for (const Segment& s : g.segments) total += s.draws.size();
    if (total > 0) {
        const auto bytes = static_cast<std::uint32_t>(total * sizeof(Prim));
        if (bytes > g.instance_capacity) {
            if (g.instances) g.d.destroy(g.instances);
            std::uint32_t cap = std::max<std::uint32_t>(64 * sizeof(Prim), g.instance_capacity);
            while (cap < bytes) cap *= 2;
            auto b = g.d.create(rhi::BufferDesc{cap, rhi::BufferUsage::vertex, "render.instances"});
            if (!b) return std::unexpected(b.error());
            g.instances = *b;
            g.instance_capacity = cap;
        }
        std::vector<Prim> all;
        all.reserve(total);
        for (const Segment& s : g.segments)
            for (const Draw& d : s.draws) all.push_back(d.prim);
        g.list.upload(g.instances, 0, std::span<const Prim>(all));
    }

    // ── the passes ───────────────────────────────────────────────────────
    const Frame frame{{W, H, static_cast<float>(g.atlas_w), static_cast<float>(g.atlas_h)}, {static_cast<float>(g.ramp_capacity), 0, 0, 0}};
    std::uint32_t first = 0;
    for (const Segment& s : g.segments) {
        g.list.begin_render({s.target, s.load, s.clear});
        if (!s.draws.empty()) {
            g.list.set_pipeline(g.pipeline);
            g.list.push_uniforms(rhi::Stage::vertex, 0, frame);
            g.list.push_uniforms(rhi::Stage::fragment, 0, frame);
            g.list.bind_vertex_buffer(0, g.instances);
            g.list.bind_texture(rhi::Stage::fragment, 0, g.ramps, g.nearest);
            g.list.bind_texture(rhi::Stage::fragment, 1, g.atlas, g.nearest);
            std::size_t i = 0;
            while (i < s.draws.size()) {
                const Draw& head = s.draws[i];
                std::size_t j = i + 1;
                while (j < s.draws.size() && s.draws[j].image == head.image && s.draws[j].linear == head.linear && s.draws[j].mask == head.mask) ++j;
                g.list.bind_texture(rhi::Stage::fragment, 2, head.image ? head.image : g.white, head.linear ? g.linear : g.nearest);
                g.list.bind_texture(rhi::Stage::fragment, 3, head.mask ? head.mask : g.white_mask, g.nearest);
                g.list.draw(6, static_cast<std::uint32_t>(j - i), 0, first + static_cast<std::uint32_t>(i));
                i = j;
            }
            first += static_cast<std::uint32_t>(s.draws.size());
        }
        g.list.end_render();
    }
    auto submitted = g.d.submit(g.list);
    if (!submitted) return std::unexpected(submitted.error());
    report.rows = g.height;
    return report;
}

}  // namespace omui::render
