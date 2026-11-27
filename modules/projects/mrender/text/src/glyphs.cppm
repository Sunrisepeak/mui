// mrender.text:glyphs — a collection's glyphs as the renderer draws them.
//
// The renderer asks for a glyph at a size and a subpixel phase; the cache
// takes its outline from the collection (mlibs.text: ems, y down) and
// rasterises it with mrender.raster, so text pixels are as deterministic as
// any other shape. Colour glyphs (COLR layers, as emoji fonts have) become
// premultiplied images composed from the font's palette.
export module mrender.text:glyphs;

import std;
import mlibs.geom;
import mlibs.text;
import mrender.raster;
import mrender.vector;

export namespace mrender::text {

class GlyphCache final : public GlyphSource {
public:
    explicit GlyphCache(mlibs::text::FontCollection& fonts);
    ~GlyphCache() override;

    GlyphMask mask(FontRef font, std::uint32_t glyph, float size, mlibs::geom::Point fraction) override;
    raster::PathData outline(FontRef font, std::uint32_t glyph) override;

    [[nodiscard]] std::size_t size() const noexcept;  // cached entries
    void clear();

    // Phases per pixel on each axis (4: positions snap to quarter pixels): the masks it tells apart.
    static constexpr int phases = 4;
    [[nodiscard]] int mask_phases() const noexcept override { return phases; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mrender::text
