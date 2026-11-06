// openmui.text:glyphs — glyph images for renderers.
//
// The renderer asks for a glyph at a size and a subpixel phase; the cache
// draws its outline (HarfBuzz draw callbacks into an openmui.raster path) and
// rasterises it with openmui.raster, so text pixels are as deterministic as
// any other shape. Colour glyphs (COLR layers, as emoji fonts have) become
// premultiplied images composed from the font's palette.
export module mlibs.text.api:glyphs;

import std;
import mlibs.geom;
import xrender.raster;
import xrender.vector;
import :font;

export namespace mlibs::text {

class GlyphCache final : public xrender::GlyphSource {
public:
    explicit GlyphCache(FontCollection& fonts);
    ~GlyphCache() override;

    xrender::GlyphMask mask(xrender::FontRef font, std::uint32_t glyph, float size, geom::Point fraction) override;
    xrender::raster::PathData outline(xrender::FontRef font, std::uint32_t glyph) override;

    [[nodiscard]] std::size_t size() const noexcept;  // cached entries
    void clear();

    // Horizontal phases per pixel (4: positions snap to quarter pixels).
    static constexpr int phases = 4;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mlibs::text
