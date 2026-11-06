// xrender.text:glyphs — a collection's glyphs as the renderer draws them.
//
// The renderer asks for a glyph at a size and a subpixel phase; the cache
// takes its outline from the collection (mlibs.text: ems, y down) and
// rasterises it with xrender.raster, so text pixels are as deterministic as
// any other shape. Colour glyphs (COLR layers, as emoji fonts have) become
// premultiplied images composed from the font's palette.
export module xrender.text:glyphs;

import std;
import mlibs.geom;
import mlibs.text;
import xrender.raster;
import xrender.vector;

export namespace xrender::text {

class GlyphCache final : public GlyphSource {
public:
    explicit GlyphCache(mlibs::text::FontCollection& fonts);
    ~GlyphCache() override;

    GlyphMask mask(FontRef font, std::uint32_t glyph, float size, mlibs::geom::Point fraction) override;
    raster::PathData outline(FontRef font, std::uint32_t glyph) override;

    [[nodiscard]] std::size_t size() const noexcept;  // cached entries
    void clear();

    // Horizontal phases per pixel (4: positions snap to quarter pixels).
    static constexpr int phases = 4;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace xrender::text
