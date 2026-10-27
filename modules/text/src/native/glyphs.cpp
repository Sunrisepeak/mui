// Glyph outlines through hb_font_draw_glyph, colour layers through the OT
// COLR/CPAL API, both rasterised by openmui.raster.
module;
#include <hb.h>
#include <hb-ot.h>
module openmui.text;

import std;
import openmui.raster;

namespace omui::typeset {

namespace {

struct Key {
    std::uint32_t font, glyph;
    std::int32_t  size64;  // size in 1/64 px
    std::uint8_t  phase_x, phase_y;
    friend bool operator==(const Key& a, const Key& b) noexcept {
        return a.font == b.font && a.glyph == b.glyph && a.size64 == b.size64 && a.phase_x == b.phase_x &&
               a.phase_y == b.phase_y;
    }
};

struct KeyHash {
    std::size_t operator()(const Key& k) const noexcept {
        std::uint64_t h = k.font;
        h = h * 1000003u ^ k.glyph;
        h = h * 1000003u ^ static_cast<std::uint32_t>(k.size64);
        h = h * 1000003u ^ (static_cast<std::uint32_t>(k.phase_x) << 8 | k.phase_y);
        return static_cast<std::size_t>(h);
    }
};

struct Entry {
    raster::Coverage                 coverage;
    std::unique_ptr<raster::Surface> color;
    geom::PixelRect                  color_bounds;
};

// Draw callbacks: font units (y up) into a path (y down), unscaled.
struct Sink {
    raster::PathBuilder* path;
};

void move_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float x, float y, void*) {
    static_cast<Sink*>(d)->path->move_to({x, -y});
}
void line_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float x, float y, void*) {
    static_cast<Sink*>(d)->path->line_to({x, -y});
}
void quad_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float cx, float cy, float x, float y, void*) {
    static_cast<Sink*>(d)->path->quad_to({cx, -cy}, {x, -y});
}
void cubic_to(hb_draw_funcs_t*, void* d, hb_draw_state_t*, float c1x, float c1y, float c2x, float c2y, float x, float y, void*) {
    static_cast<Sink*>(d)->path->cubic_to({c1x, -c1y}, {c2x, -c2y}, {x, -y});
}
void close_path(hb_draw_funcs_t*, void* d, hb_draw_state_t*, void*) { static_cast<Sink*>(d)->path->close(); }

hb_draw_funcs_t* draw_funcs() {
    static hb_draw_funcs_t* funcs = [] {
        hb_draw_funcs_t* f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(f, move_to, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(f, line_to, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(f, quad_to, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(f, cubic_to, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(f, close_path, nullptr, nullptr);
        hb_draw_funcs_make_immutable(f);
        return f;
    }();
    return funcs;
}

}  // namespace

struct GlyphCache::Impl {
    FontCollection* fonts;
    std::unordered_map<Key, Entry, KeyHash> entries;
    // Outlines in em units (1 em = 1.0), per (font, glyph).
    std::unordered_map<std::uint64_t, std::unique_ptr<raster::PathBuilder>> outlines;

    raster::PathBuilder& outline_of(render::FontRef font, std::uint32_t glyph) {
        std::uint64_t key = (static_cast<std::uint64_t>(font.id) << 32) | glyph;
        auto& slot = outlines[key];
        if (!slot) {
            slot = std::make_unique<raster::PathBuilder>();
            raster::PathBuilder units;
            Sink sink{&units};
            auto* hb = static_cast<hb_font_t*>(fonts->hb_font(font));
            hb_font_draw_glyph(hb, glyph, draw_funcs(), &sink);
            // Scale font units to ems.
            float k = 1.0f / static_cast<float>(fonts->face(fonts->face_of(font)).units_per_em);
            auto d = units.data();
            std::size_t pi = 0;
            for (raster::Verb v : d.verbs) {
                auto pt = [&] {
                    geom::Point p = d.points[pi++];
                    return geom::Point{p.x * k, p.y * k};
                };
                switch (v) {
                    case raster::Verb::move: slot->move_to(pt()); break;
                    case raster::Verb::line: slot->line_to(pt()); break;
                    case raster::Verb::quad: {
                        auto c = pt();
                        slot->quad_to(c, pt());
                        break;
                    }
                    case raster::Verb::cubic: {
                        auto c1 = pt();
                        auto c2 = pt();
                        slot->cubic_to(c1, c2, pt());
                        break;
                    }
                    case raster::Verb::close: slot->close(); break;
                }
            }
        }
        return *slot;
    }
};

GlyphCache::GlyphCache(FontCollection& fonts) : impl_(std::make_unique<Impl>()) { impl_->fonts = &fonts; }
GlyphCache::~GlyphCache() = default;

std::size_t GlyphCache::size() const noexcept { return impl_->entries.size(); }
void GlyphCache::clear() {
    impl_->entries.clear();
    impl_->outlines.clear();
}

raster::PathData GlyphCache::outline(render::FontRef font, std::uint32_t glyph) {
    return impl_->outline_of(font, glyph).data();
}

render::GlyphMask GlyphCache::mask(render::FontRef font, std::uint32_t glyph, float size, geom::Point fraction) {
    auto quant = [](float f) {
        int q = static_cast<int>(std::floor(f * phases + 0.5f));
        return static_cast<std::uint8_t>(std::clamp(q, 0, phases - 1));
    };
    Key key{font.id, glyph, static_cast<std::int32_t>(std::floor(size * 64 + 0.5f)), quant(fraction.x), quant(fraction.y)};
    auto it = impl_->entries.find(key);
    if (it == impl_->entries.end()) {
        Entry e;
        float px = static_cast<float>(key.size64) / 64.0f;
        float ox = static_cast<float>(key.phase_x) / phases, oy = static_cast<float>(key.phase_y) / phases;
        geom::Affine m = geom::Affine::translate(ox, oy) * geom::Affine::scale(px, px);
        geom::PixelRect everywhere{-4096, -4096, 8192, 8192};
        auto* hb = static_cast<hb_font_t*>(impl_->fonts->hb_font(font));
        hb_face_t* face = hb_font_get_face(hb);
        unsigned layers = hb_ot_color_glyph_get_layers(face, glyph, 0, nullptr, nullptr);
        if (layers > 0) {
            // COLR v0: the layers in order, each a glyph filled with a palette colour.
            std::vector<hb_ot_color_layer_t> ls(layers);
            unsigned count = layers;
            hb_ot_color_glyph_get_layers(face, glyph, 0, &count, ls.data());
            unsigned colors = hb_ot_color_palette_get_colors(face, 0, 0, nullptr, nullptr);
            std::vector<hb_color_t> palette(colors);
            hb_ot_color_palette_get_colors(face, 0, 0, &colors, palette.data());
            std::vector<raster::Coverage> covs(count);
            geom::PixelRect box{};
            for (unsigned i = 0; i < count; ++i) {
                raster::fill_coverage(impl_->outline_of(font, ls[i].glyph).data(), m, raster::FillRule::nonzero, everywhere, covs[i]);
                if (covs[i].empty()) continue;
                if (box.empty()) box = covs[i].bounds;
                else {
                    int l = std::min(box.x, covs[i].bounds.x), t = std::min(box.y, covs[i].bounds.y);
                    int r = std::max(box.x + box.width, covs[i].bounds.x + covs[i].bounds.width);
                    int b = std::max(box.y + box.height, covs[i].bounds.y + covs[i].bounds.height);
                    box = {l, t, r - l, b - t};
                }
            }
            if (!box.empty()) {
                e.color = std::make_unique<raster::Surface>(box.width, box.height);
                e.color_bounds = box;
                raster::Canvas canvas(*e.color);
                for (unsigned i = 0; i < count; ++i) {
                    if (covs[i].empty()) continue;
                    geom::Color c = geom::Color::black();
                    if (ls[i].color_index < palette.size()) {
                        hb_color_t hc = palette[ls[i].color_index];
                        c = geom::Color::rgba8(hb_color_get_red(hc), hb_color_get_green(hc), hb_color_get_blue(hc),
                                               hb_color_get_alpha(hc));
                    }
                    canvas.fill_coverage(covs[i], raster::Paint::solid(c), -box.x, -box.y);
                }
            }
        } else {
            raster::fill_coverage(impl_->outline_of(font, glyph).data(), m, raster::FillRule::nonzero, everywhere, e.coverage);
        }
        it = impl_->entries.emplace(key, std::move(e)).first;
    }
    const Entry& e = it->second;
    if (e.color) return {nullptr, e.color.get(), e.color_bounds};
    return {&e.coverage, nullptr, {}};
}

}  // namespace omui::typeset
