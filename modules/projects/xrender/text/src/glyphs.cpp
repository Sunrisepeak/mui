// The glyph cache: outlines from the collection, rasterised by xrender.raster;
// colour layers composed from the font's palette.
module xrender.text;

import std;
import mlibs.geom;
import mlibs.text;
import xrender.raster;
import xrender.vector;

namespace xrender::text {

namespace geom = mlibs::geom;

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

}  // namespace

struct GlyphCache::Impl {
    mlibs::text::FontCollection* fonts;
    std::unordered_map<Key, Entry, KeyHash> entries;
    // Outlines in em units (1 em = 1.0), per (font, glyph).
    std::unordered_map<std::uint64_t, std::unique_ptr<raster::PathBuilder>> outlines;

    raster::PathBuilder& outline_of(FontRef font, std::uint32_t glyph) {
        std::uint64_t key = (static_cast<std::uint64_t>(font.id) << 32) | glyph;
        auto& slot = outlines[key];
        if (!slot) {
            slot = std::make_unique<raster::PathBuilder>();
            const mlibs::text::GlyphOutline o = fonts->outline({font.id}, glyph);
            std::size_t pi = 0;
            for (raster::Verb v : o.verbs) {
                auto pt = [&] { return o.points[pi++]; };
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

GlyphCache::GlyphCache(mlibs::text::FontCollection& fonts) : impl_(std::make_unique<Impl>()) { impl_->fonts = &fonts; }
GlyphCache::~GlyphCache() = default;

std::size_t GlyphCache::size() const noexcept { return impl_->entries.size(); }
void GlyphCache::clear() {
    impl_->entries.clear();
    impl_->outlines.clear();
}

raster::PathData GlyphCache::outline(FontRef font, std::uint32_t glyph) {
    return impl_->outline_of(font, glyph).data();
}

GlyphMask GlyphCache::mask(FontRef font, std::uint32_t glyph, float size, geom::Point fraction) {
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
        const std::vector<mlibs::text::ColorLayer> ls = impl_->fonts->color_layers({font.id}, glyph);
        const std::size_t count = ls.size();
        if (count > 0) {
            // COLR v0: the layers in order, each a glyph filled with a palette colour.
            std::vector<raster::Coverage> covs(count);
            geom::PixelRect box{};
            for (std::size_t i = 0; i < count; ++i) {
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
                for (std::size_t i = 0; i < count; ++i) {
                    if (covs[i].empty()) continue;
                    canvas.fill_coverage(covs[i], raster::Paint::solid(ls[i].color), -box.x, -box.y);
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

}  // namespace xrender::text
