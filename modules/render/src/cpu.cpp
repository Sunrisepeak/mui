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

std::string_view command_kind_name(std::size_t index) noexcept {
    static constexpr std::array<std::string_view, std::variant_size_v<Command>> names{
        "fill_rect", "fill_round_rect", "stroke_round_rect", "box_shadow", "fill_path", "stroke_path", "draw_image", "draw_glyphs",
        "push_clip_rect", "push_clip_path", "pop_clip", "push_transform", "pop_transform", "push_layer", "pop_layer",
        "external_texture", "begin_node", "end_node"};
    return index < names.size() ? names[index] : std::string_view{};
}

struct CpuRenderer::Scratch {
    raster::Canvas             canvas;
    std::vector<Affine>        transforms;
    std::vector<std::uint32_t> nodes;
    PathConv                   paths;
    PaintConv                  paints;
    Path                       path;
    explicit Scratch(raster::Surface& target) : canvas(target) {}
};

namespace {

// The glyph masks and outlines of a frame, looked up by the calling thread
// before the bands are drawn at the same time: the bands then only read this
// table (the text engine's cache is not for several threads).
class Resolved final : public GlyphSource {
public:
    void begin(GlyphSource* source) {
        source_ = source;
        std::ranges::fill(masks_, MaskSlot{});
        std::ranges::fill(outlines_, OutlineSlot{});
        mask_count_ = outline_count_ = 0;
    }
    GlyphMask mask(FontRef font, std::uint32_t glyph, float size, Point fraction) override {
        const MaskKey k{font.id, glyph, size, fraction.x, fraction.y};
        if (resolving_) {
            if (MaskSlot* s = find(k, true); s && !s->used) {
                *s = {k, source_->mask(font, glyph, size, fraction), true};
                ++mask_count_;
            }
            return {};
        }
        const MaskSlot* s = find(k, false);  // read only: bands call this at the same time
        return s && s->used ? s->mask : GlyphMask{};
    }
    raster::PathData outline(FontRef font, std::uint32_t glyph) override {
        const std::uint64_t k = (static_cast<std::uint64_t>(font.id) << 32) | glyph;
        if (resolving_) {
            if (OutlineSlot* s = find_outline(k, true); s && !s->used) {
                *s = {k, source_->outline(font, glyph), true};
                ++outline_count_;
            }
            return {};
        }
        const OutlineSlot* s = find_outline(k, false);
        return s && s->used ? s->data : raster::PathData{};
    }
    // While resolving, calls go to the source and are recorded; afterwards
    // they are answered from the record.
    bool resolving_ = false;
    void reserve(std::size_t glyphs) {
        std::size_t n = 64;
        while (n < 2 * glyphs) n *= 2;
        if (masks_.size() < n) masks_.resize(n);
        if (outlines_.size() < n) outlines_.resize(n);
    }

private:
    struct MaskKey {
        std::uint32_t font, glyph;
        float         size, fx, fy;
        friend bool operator==(const MaskKey& a, const MaskKey& b) noexcept {
            return a.font == b.font && a.glyph == b.glyph && a.size == b.size && a.fx == b.fx && a.fy == b.fy;
        }
    };
    struct MaskSlot {
        MaskKey   key{};
        GlyphMask mask{};
        bool      used = false;
    };
    struct OutlineSlot {
        std::uint64_t    key = 0;
        raster::PathData data{};
        bool             used = false;
    };
    static std::uint64_t hash(const MaskKey& k) noexcept {
        std::uint64_t h = 0xcbf29ce484222325ull;
        for (std::uint32_t v : {k.font, k.glyph, std::bit_cast<std::uint32_t>(k.size), std::bit_cast<std::uint32_t>(k.fx),
                                std::bit_cast<std::uint32_t>(k.fy)})
            h = (h ^ v) * 0x100000001b3ull;
        return h;
    }
    MaskSlot* find(const MaskKey& k, bool insert) {
        if (masks_.empty()) return nullptr;
        if (insert && 2 * (mask_count_ + 1) > masks_.size()) grow_masks();
        const std::size_t mask = masks_.size() - 1;
        for (std::size_t i = hash(k) & mask;; i = (i + 1) & mask) {
            if (!masks_[i].used) return insert ? &masks_[i] : nullptr;
            if (masks_[i].key == k) return &masks_[i];
        }
    }
    OutlineSlot* find_outline(std::uint64_t k, bool insert) {
        if (outlines_.empty()) return nullptr;
        if (insert && 2 * (outline_count_ + 1) > outlines_.size()) grow_outlines();
        const std::size_t mask = outlines_.size() - 1;
        for (std::size_t i = (k * 0x9e3779b97f4a7c15ull) & mask;; i = (i + 1) & mask) {
            if (!outlines_[i].used) return insert ? &outlines_[i] : nullptr;
            if (outlines_[i].key == k) return &outlines_[i];
        }
    }
    void grow_masks() {
        std::vector<MaskSlot> old(masks_.size() * 2);
        old.swap(masks_);
        mask_count_ = 0;
        for (const MaskSlot& s : old)
            if (s.used) {
                *find(s.key, true) = s;
                ++mask_count_;
            }
    }
    void grow_outlines() {
        std::vector<OutlineSlot> old(outlines_.size() * 2);
        old.swap(outlines_);
        outline_count_ = 0;
        for (const OutlineSlot& s : old)
            if (s.used) {
                *find_outline(s.key, true) = s;
                ++outline_count_;
            }
    }

    GlyphSource*             source_ = nullptr;
    std::vector<MaskSlot>    masks_;
    std::vector<OutlineSlot> outlines_;
    std::size_t              mask_count_ = 0, outline_count_ = 0;
};

// Where a glyph's mask is asked for and placed: one function for the bands
// and for the pass that resolves their glyphs first, so both ask the same.
bool simple_glyphs(const Affine& m) noexcept { return m.b == 0 && m.c == 0 && m.a == m.d && m.a > 0; }

struct MaskCall {
    float ix, iy;
    float size;
    Point fraction;
};

MaskCall mask_call(const DrawGlyphs& c, const Glyph& g, const Affine& m) noexcept {
    const Point d = m.apply({c.origin.x + g.position.x, c.origin.y + g.position.y});
    const float ix = std::floor(d.x), iy = std::floor(d.y);
    return {ix, iy, c.size * m.a, {d.x - ix, d.y - iy}};
}

// Asks `glyphs` for every mask and outline the bands of `tasks` will draw.
void resolve_glyphs(const DisplayList& list, GlyphSource& glyphs, float scale, std::span<const RowSpan> tasks,
                    std::vector<Affine>& transforms) {
    transforms.assign(1, Affine::scale(scale, scale));
    for (const Command& command : list.commands()) {
        if (const auto* t = std::get_if<PushTransform>(&command)) {
            transforms.push_back(transforms.back() * t->transform);
            continue;
        }
        if (std::holds_alternative<PopTransform>(command)) {
            if (transforms.size() > 1) transforms.pop_back();
            continue;
        }
        const auto* c = std::get_if<DrawGlyphs>(&command);
        if (!c) continue;
        const Affine& m = transforms.back();
        const auto b = device_bounds(command, m, nullptr);
        if (!b || std::ranges::none_of(tasks, [&](RowSpan r) { return b->bottom() > static_cast<float>(r.top) && b->y < static_cast<float>(r.bottom); }))
            continue;
        const bool simple = simple_glyphs(m);
        for (const Glyph& g : c->glyphs) {
            if (simple) {
                const MaskCall call = mask_call(*c, g, m);
                (void)glyphs.mask(c->font, g.id, call.size, call.fraction);
            } else {
                (void)glyphs.outline(c->font, g.id);
            }
        }
    }
}

}  // namespace

struct CpuRenderer::Impl {
    std::vector<std::unique_ptr<Scratch>> pool;    // one canvas per band drawn at the same time
    std::vector<Scratch*>                 free;
    std::mutex                            lock;
    std::vector<std::uint32_t>            ids;     // object ids, shared by the canvases
    bool                                  ids_on = false;
    std::vector<RowSpan>                  tasks;
    std::vector<Affine>                   transforms;  // the glyph pass's
    std::vector<RenderReport>             reports;     // one per task
    Resolved                              resolved;

    Scratch& take(raster::Surface& target) {
        std::scoped_lock g(lock);
        if (free.empty()) {
            pool.push_back(std::make_unique<Scratch>(target));
            return *pool.back();
        }
        Scratch* s = free.back();
        free.pop_back();
        return *s;
    }
    void give(Scratch& s) {
        std::scoped_lock g(lock);
        free.push_back(&s);
    }
};

CpuRenderer::CpuRenderer() : impl_(std::make_unique<Impl>()) {}
CpuRenderer::CpuRenderer(CpuRenderer&&) noexcept = default;
CpuRenderer& CpuRenderer::operator=(CpuRenderer&&) noexcept = default;
CpuRenderer::~CpuRenderer() = default;

std::span<const std::uint32_t> CpuRenderer::object_ids() const noexcept {
    if (!impl_ || !impl_->ids_on) return {};
    return impl_->ids;
}

RenderReport CpuRenderer::render(const DisplayList& list, raster::Surface& target, const Resources& res, const CpuOptions& options) {
    RenderReport report;
    Impl& d = *impl_;
    d.ids_on = options.object_ids;
    const std::size_t pixels = static_cast<std::size_t>(target.width()) * static_cast<std::size_t>(target.height());
    if (options.object_ids && d.ids.size() != pixels) d.ids.assign(pixels, 0);

    // The bands to draw, cut into tasks of at most rows_per_task rows.
    d.tasks.clear();
    auto add = [&](RowSpan band) {
        band.top = std::clamp(band.top, 0, target.height());
        band.bottom = std::clamp(band.bottom, band.top, target.height());
        if (band.bottom <= band.top) return;
        const std::int32_t step = options.parallel ? std::max(8, options.rows_per_task) : band.bottom - band.top;
        for (std::int32_t y = band.top; y < band.bottom; y += step) d.tasks.push_back({y, std::min(band.bottom, y + step)});
    };
    const bool whole = options.rows.empty();
    if (whole) add({0, target.height()});
    else
        for (RowSpan band : options.rows) add(band);

    if (!options.parallel || d.tasks.size() < 2) {
        Scratch& sc = d.take(target);
        if (whole && !options.parallel) draw(sc, list, target, res, options, {0, target.height()}, true, report);
        else
            for (RowSpan band : d.tasks) draw(sc, list, target, res, options, band, false, report);
        d.give(sc);
        return report;
    }

    // Glyphs first, on this thread; then the bands at the same time.
    Resources shared = res;
    if (res.glyphs) {
        std::size_t glyphs = 0;
        for (const Command& c : list.commands())
            if (const auto* g = std::get_if<DrawGlyphs>(&c)) glyphs += g->glyphs.size();
        d.resolved.reserve(glyphs);
        d.resolved.begin(res.glyphs);
        d.resolved.resolving_ = true;
        resolve_glyphs(list, d.resolved, options.scale, d.tasks, d.transforms);
        d.resolved.resolving_ = false;
        shared.glyphs = &d.resolved;
    }
    d.reports.assign(d.tasks.size(), RenderReport{});
    options.parallel->run(d.tasks.size(), [&](std::size_t i) {
        Scratch& sc = d.take(target);
        draw(sc, list, target, shared, options, d.tasks[i], false, d.reports[i]);
        d.give(sc);
    });
    for (const RenderReport& r : d.reports) {
        for (std::size_t k = 0; k < r.kinds.size(); ++k) {
            report.kinds[k].count += r.kinds[k].count;
            report.kinds[k].ms += r.kinds[k].ms;
        }
        report.commands += r.commands;
        report.effects_skipped += r.effects_skipped;
        report.external_skipped += r.external_skipped;
        report.missing_images += r.missing_images;
        report.missing_glyphs += r.missing_glyphs;
        report.rows += r.rows;
        report.skipped += r.skipped;
    }
    return report;
}

void CpuRenderer::draw(Scratch& sc, const DisplayList& list, raster::Surface& target, const Resources& res, const CpuOptions& options,
                       RowSpan band, bool whole, RenderReport& report) {
    raster::Canvas& canvas = sc.canvas;
    canvas.reset(target);
    if (!whole) canvas.restrict_rows(band.top, band.bottom);
    report.rows += static_cast<std::size_t>(band.bottom - band.top);
    if (options.clear) {
        const raster::Rgba8 bg = raster::premultiply(options.background);
        if (whole) target.clear(bg);
        else
            for (std::int32_t y = band.top; y < band.bottom; ++y) std::fill_n(target.row(y), target.width(), bg);
    }
    if (options.object_ids) {
        canvas.use_object_ids(impl_->ids);
        canvas.clear_object_ids(band.top, band.bottom);
    }

    auto& transforms = sc.transforms;
    auto& nodes = sc.nodes;
    auto& paths = sc.paths;
    auto& paints = sc.paints;
    transforms.assign(1, Affine::scale(options.scale, options.scale));
    nodes.assign(1, 0);

    auto m = [&]() -> const Affine& { return transforms.back(); };
    // The paint's own transform is in logical space; the device needs it after the current matrix.
    auto device_paint = [&](const Paint& p) {
        raster::Paint rp = paints.convert(p, res, report);
        if (rp.kind != raster::Paint::Kind::solid) rp.transform = m() * rp.transform;
        return rp;
    };

    for (const Command& command : list.commands()) {
        ++report.commands;
        const auto measured_from = options.measure ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        struct Measure {
            const CpuOptions&                     o;
            RenderReport&                         r;
            std::size_t                           kind;
            std::chrono::steady_clock::time_point from;
            ~Measure() {
                if (!o.measure) return;
                r.kinds[kind].count += 1;
                r.kinds[kind].ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
            }
        } measure{options, report, command.index(), measured_from};
        if (!whole)
            if (auto b = device_bounds(command, m(), nullptr); b && (b->bottom() <= static_cast<float>(band.top) || b->y >= static_cast<float>(band.bottom))) {
                ++report.skipped;  // cannot reach these rows
                continue;
            }
        std::visit(
            [&](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::same_as<T, FillRect>) {
                    canvas.fill_rect(c.rect, device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, FillRoundRect>) {
                    canvas.fill_round_rect(c.rect, c.radii, device_paint(c.paint), m());
                } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                    sc.path.clear();
                    sc.path.add_rounded_rect(c.rect, c.radii);
                    canvas.stroke_path(paths.data(sc.path.view()), {.width = c.width}, device_paint(c.paint), m());
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
                    const bool simple = simple_glyphs(mm);
                    for (const Glyph& g : c.glyphs) {
                        Point pen{c.origin.x + g.position.x, c.origin.y + g.position.y};
                        if (simple) {
                            const MaskCall call = mask_call(c, g, mm);
                            const float ix = call.ix, iy = call.iy;
                            GlyphMask gm = res.glyphs->mask(c.font, g.id, call.size, call.fraction);
                            if (gm.color) {
                                const auto& b = gm.color_bounds;
                                canvas.draw_image(gm.color->view(), {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                                  {ix + b.x, iy + b.y, static_cast<float>(b.width), static_cast<float>(b.height)},
                                                  raster::Sampling::nearest, c.paint.color.a);
                                continue;
                            }
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
}

}  // namespace omui::render
