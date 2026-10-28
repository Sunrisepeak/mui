module openmui.render.vector;

import std;
import openmui.raster;
import :shared;

namespace omui::render {

namespace {

static_assert(static_cast<int>(PathVerb::close) == static_cast<int>(raster::Verb::close));
static_assert(static_cast<int>(BlendMode::erase) == static_cast<int>(raster::Blend::erase),
              "render and raster list the blend modes in the same order");

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

// Asks `glyphs` for every mask and outline the bands of `tasks` will draw.
void resolve_glyphs(const DisplayList& list, GlyphSource& glyphs, const Affine& base, std::span<const RowSpan> tasks,
                    std::vector<Affine>& transforms) {
    transforms.assign(1, base);
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
        for (std::size_t i = 0; i < c->glyphs.size(); ++i) {
            const Glyph& g = c->glyphs[i];
            const Placed pl = place(*c, i, m);
            if (pl.style && pl.style->opacity <= 0) continue;
            if (pl.mask) {
                const MaskCall call = mask_call(pl.pen, c->size, m);
                (void)glyphs.mask(c->font, g.id, call.size, call.fraction);
            } else {
                if (glyphs.outline(c->font, g.id).verbs.empty())
                    (void)glyphs.mask(c->font, g.id, c->size * m.max_scale(), {0, 0});  // a colour glyph's image
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
    // The effect layers of the list being drawn, prepared before the bands:
    // where each begins and ends in the list, where its result goes on the
    // surface (device pixels), and the result.
    // Kept between frames (the first `effect_count` are this frame's), with
    // their buffers: a steady animation allocates nothing.
    struct EffectLayer {
        std::size_t                  push = 0, pop = 0;
        Rect                         device;
        raster::Surface              source, result;
        DisplayList                  inner;
        std::unique_ptr<CpuRenderer> offscreen;
        // An effect that reads its backdrop runs when the draw reaches it,
        // over what the target holds there: what it needs, kept until then.
        const EffectInfo*            info = nullptr;
        bool                         backdrop = false;
        std::vector<float>           params;
        float                        scale = 1;
        Rect                         layer;   // in the surface
        std::vector<EffectShape>     shapes;  // in the surface
        raster::Surface              under;
    };
    std::vector<EffectLayer> effect_layers;
    std::size_t              effect_count = 0;

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
    prepare_effects(list, target, res, options, report);
    const bool whole = options.rows.empty();
    // An effect that reads its backdrop reads the target where the draw
    // reaches it: the frame is drawn in one band, in order, and a band that
    // reaches such an effect's surface takes all of its rows (what lies
    // under it is drawn again, not the last frame's glass).
    bool backdrops = false;
    for (std::size_t i = 0; i < d.effect_count; ++i) backdrops = backdrops || (d.effect_layers[i].backdrop && d.effect_layers[i].device.width > 0);
    if (backdrops) {
        RowSpan band{0, target.height()};
        if (!whole) {
            band = options.rows.front();
            for (RowSpan b : options.rows) band = {std::min(band.top, b.top), std::max(band.bottom, b.bottom)};
            for (bool grew = true; grew;) {
                grew = false;
                for (std::size_t i = 0; i < d.effect_count; ++i) {
                    const auto& e = d.effect_layers[i];
                    if (!e.backdrop || e.device.width <= 0) continue;
                    const auto top = static_cast<std::int32_t>(e.device.y), bottom = static_cast<std::int32_t>(e.device.y + e.device.height);
                    if (bottom <= band.top || top >= band.bottom) continue;
                    if (top < band.top || bottom > band.bottom) {
                        band = {std::min(band.top, top), std::max(band.bottom, bottom)};
                        grew = true;
                    }
                }
            }
        }
        band.top = std::clamp(band.top, 0, target.height());
        band.bottom = std::clamp(band.bottom, band.top, target.height());
        Scratch& sc = d.take(target);
        if (band.bottom > band.top) draw(sc, list, target, res, options, band, band.top == 0 && band.bottom == target.height(), report);
        d.give(sc);
        return report;
    }
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
        resolve_glyphs(list, d.resolved, options.transform.value_or(Affine::scale(options.scale, options.scale)), d.tasks, d.transforms);
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

void CpuRenderer::prepare_effects(const DisplayList& list, const raster::Surface& target, const Resources& res, const CpuOptions& options,
                                  RenderReport& report) {
    Impl& d = *impl_;
    d.effect_count = 0;
    const auto& all = list.commands();
    std::vector<Affine> transforms{options.transform.value_or(Affine::scale(options.scale, options.scale))};
    // Logical pixels to device pixels, for the effects' sizes and reaches.
    const float scale = transforms.front().max_scale();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Command& c = all[i];
        if (const auto* t = std::get_if<PushTransform>(&c)) {
            transforms.push_back(transforms.back() * t->transform);
            continue;
        }
        if (std::holds_alternative<PopTransform>(c)) {
            if (transforms.size() > 1) transforms.pop_back();
            continue;
        }
        const auto* push = std::get_if<PushLayer>(&c);
        if (!push || push->effect.id == 0) continue;
        // Its pop: the matching one at the same depth.
        std::size_t pop = i + 1;
        for (int depth = 1; pop < all.size(); ++pop) {
            if (std::holds_alternative<PushLayer>(all[pop])) ++depth;
            else if (std::holds_alternative<PopLayer>(all[pop]) && --depth == 0) break;
        }
        if (pop >= all.size()) break;  // unbalanced: drawn as an ordinary layer
        const EffectInfo* info = effects().info(push->effect);
        if (!info || !info->cpu) {
            ++report.effects_skipped;
            continue;  // drawn without it
        }
        std::vector<float> params(push->params.begin(), push->params.end());
        for (std::size_t k = params.size(); k < info->params.size(); ++k) params.push_back(info->params[k].value);
        const float reach = std::ceil(effects().reach(push->effect, params) * scale) + 1;
        const Rect layer = transforms.back().map(push->bounds);
        const Rect screen{-reach, -reach, static_cast<float>(target.width()) + 2 * reach, static_cast<float>(target.height()) + 2 * reach};
        const Rect wanted = Rect{layer.x - reach, layer.y - reach, layer.width + 2 * reach, layer.height + 2 * reach}.intersection(screen);
        if (d.effect_count == d.effect_layers.size()) d.effect_layers.emplace_back();
        Impl::EffectLayer& out = d.effect_layers[d.effect_count++];
        out.push = i;
        out.pop = pop;
        out.device = {};
        if (!wanted.empty()) {
            const float x0 = std::floor(wanted.x), y0 = std::floor(wanted.y);
            const int w = static_cast<int>(std::ceil(wanted.right()) - x0), h = static_cast<int>(std::ceil(wanted.bottom()) - y0);
            out.device = {x0, y0, static_cast<float>(w), static_cast<float>(h)};
            // The layer's content, whole, under the transform it had, offscreen.
            out.inner.clear();
            for (std::size_t k = i + 1; k < pop; ++k) out.inner.append_command(all[k]);
            if (out.source.width() != w || out.source.height() != h) out.source.resize(w, h);
            if (out.result.width() != w || out.result.height() != h) out.result.resize(w, h);
            out.result.clear();
            if (!out.offscreen) out.offscreen = std::make_unique<CpuRenderer>();
            CpuOptions o;
            o.transform = Affine::translate(-x0, -y0) * transforms.back();
            o.background = Color::transparent();
            const RenderReport r = out.offscreen->render(out.inner, out.source, res, o);
            report.missing_glyphs += r.missing_glyphs;
            report.missing_images += r.missing_images;
            report.effects_skipped += r.effects_skipped;
            // The shapes in the surface's pixels: the layer's, or its bounds.
            const Affine& m = transforms.back();
            const float k = m.max_scale();
            out.shapes.clear();
            for (const EffectShape& sh : push->shapes) {
                const Rect r = m.map(sh.rect).offset({-x0, -y0});
                out.shapes.push_back({r, {sh.radii.top_left * k, sh.radii.top_right * k, sh.radii.bottom_right * k, sh.radii.bottom_left * k}});
            }
            if (out.shapes.empty()) out.shapes.push_back({layer.offset({-x0, -y0}), {}});
            out.info = info;
            out.backdrop = info->backdrop;
            out.params = params;
            out.scale = scale;
            out.layer = layer.offset({-x0, -y0});
            if (!out.backdrop) run_cpu_effect(*info, {out.source, out.result, params, scale, out.layer, nullptr, out.shapes}, options.parallel);
        }
        i = pop;  // nested effect layers are the offscreen render's
    }
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
    transforms.assign(1, options.transform.value_or(Affine::scale(options.scale, options.scale)));
    nodes.assign(1, 0);

    auto m = [&]() -> const Affine& { return transforms.back(); };
    // The paint's own transform is in logical space; the device needs it after the current matrix.
    auto device_paint = [&](const Paint& p) {
        raster::Paint rp = paints.convert(p, res, report);
        if (rp.kind != raster::Paint::Kind::solid) rp.transform = m() * rp.transform;
        return rp;
    };

    const auto& all = list.commands();
    std::size_t next_effect = 0;
    for (std::size_t index = 0; index < all.size(); ++index) {
        const Command& command = all[index];
        // A prepared effect layer: its result, then on after its pop (D-68).
        while (next_effect < impl_->effect_count && impl_->effect_layers[next_effect].pop < index) ++next_effect;
        if (next_effect < impl_->effect_count && impl_->effect_layers[next_effect].push == index) {
            auto& layer = impl_->effect_layers[next_effect];
            const auto& push = std::get<PushLayer>(command);
            if (layer.backdrop && layer.device.width > 0) {
                // What the target holds under the surface, now; transparent beyond it.
                const int w = layer.source.width(), h = layer.source.height();
                if (layer.under.width() != w || layer.under.height() != h) layer.under.resize(w, h);
                layer.under.clear();
                const auto x0 = static_cast<int>(layer.device.x), y0 = static_cast<int>(layer.device.y);
                for (int y = 0; y < h; ++y) {
                    const int ty = y0 + y;
                    if (ty < 0 || ty >= target.height()) continue;
                    const int from = std::max(0, -x0), to = std::min(w, target.width() - x0);
                    if (to > from) std::copy_n(target.row(ty) + x0 + from, to - from, layer.under.row(y) + from);
                }
                if (layer.info->backdrop_blur)
                    box_blur(layer.under, box_radius(layer.info->backdrop_blur(layer.params), layer.scale), options.parallel);
                layer.result.clear();
                // The frame is drawn in one band here (it reads its backdrop): the effect's own rows go to the pool.
                run_cpu_effect(*layer.info, {layer.source, layer.result, layer.params, layer.scale, layer.layer, &layer.under, layer.shapes}, options.parallel);
            }
            const bool blended = push.blend != BlendMode::normal;
            if (blended) canvas.push_layer(1, static_cast<raster::Blend>(push.blend));
            if (layer.device.width > 0 && layer.result.width() > 0)
                canvas.draw_image(layer.result.view(), {0, 0, static_cast<float>(layer.result.width()), static_cast<float>(layer.result.height())},
                                  layer.device, raster::Sampling::nearest, push.opacity, Affine{});
            if (blended) canvas.pop_layer();
            report.commands += layer.pop - layer.push + 1;
            index = layer.pop;
            continue;
        }
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
                    if (!(c.width > 0)) return;
                    const Ring ring = stroke_ring(c.rect, c.radii, c.width);
                    sc.path.clear();
                    sc.path.add_rounded_rect(ring.outer, ring.outer_radii);
                    if (ring.hollow) sc.path.add_rounded_rect(ring.inner, ring.inner_radii);
                    canvas.fill_path(paths.data(sc.path.view()), raster::FillRule::even_odd, device_paint(c.paint), m());
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
                    const raster::Paint run_paint = device_paint(c.paint);
                    const Affine& mm = m();
                    for (std::size_t i = 0; i < c.glyphs.size(); ++i) {
                        const Glyph& g = c.glyphs[i];
                        const Placed pl = place(c, i, mm);
                        float opacity = 1;
                        raster::Paint paint = run_paint;
                        if (pl.style) {
                            // The glyph's own opacity and tint over the run's colour (D-68).
                            opacity = std::clamp(pl.style->opacity, 0.0f, 1.0f);
                            if (opacity <= 0) continue;
                            if (c.paint.kind == Paint::Kind::solid) {
                                const Color t = pl.style->tint;
                                const float k = std::clamp(t.a, 0.0f, 1.0f);
                                Color col = c.paint.color;
                                col = {col.r + (t.r - col.r) * k, col.g + (t.g - col.g) * k, col.b + (t.b - col.b) * k, col.a * opacity};
                                paint = device_paint(Paint::solid(col));
                            } else if (opacity < 1) {
                                canvas.push_layer(opacity, raster::Blend::normal);
                            }
                        }
                        const bool layered = pl.style && c.paint.kind != Paint::Kind::solid && opacity < 1;
                        if (pl.mask) {
                            const MaskCall call = mask_call(pl.pen, c.size, mm);
                            GlyphMask gm = res.glyphs->mask(c.font, g.id, call.size, call.fraction);
                            if (gm.color) {
                                const auto& b = gm.color_bounds;
                                canvas.draw_image(gm.color->view(), {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                                  {call.ix + b.x, call.iy + b.y, static_cast<float>(b.width), static_cast<float>(b.height)},
                                                  raster::Sampling::nearest, c.paint.color.a * opacity);
                            } else if (gm.coverage) {
                                canvas.fill_coverage(*gm.coverage, paint, static_cast<int>(call.ix), static_cast<int>(call.iy));
                            } else {
                                ++report.missing_glyphs;
                            }
                        } else {
                            raster::PathData outline = res.glyphs->outline(c.font, g.id);
                            if (!outline.verbs.empty()) {
                                canvas.fill_path(outline, raster::FillRule::nonzero, paint, pl.outline);
                            } else {
                                // A colour glyph (emoji) has an image, not an outline: drawn at
                                // device size, then under the glyph's transform.
                                const float s = mm.max_scale();
                                GlyphMask gm = res.glyphs->mask(c.font, g.id, c.size * s, {0, 0});
                                if (gm.color) {
                                    const auto& b = gm.color_bounds;
                                    const Affine to = mm * Affine::translate(pl.pen.x, pl.pen.y) *
                                                      (pl.style ? pl.style->transform : Affine{}) * Affine::scale(1 / s, 1 / s);
                                    canvas.draw_image(gm.color->view(), {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                                      {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.width),
                                                       static_cast<float>(b.height)},
                                                      raster::Sampling::linear, c.paint.color.a * opacity, to);
                                } else {
                                    ++report.missing_glyphs;
                                }
                            }
                        }
                        if (layered) canvas.pop_layer();
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
                    // An effect that is not registered (counted by prepare_effects): the layer without it.
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
