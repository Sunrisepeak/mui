// openmui.text:glyphs — glyph images for renderers.
//
// The renderer asks for a glyph at a size and a subpixel phase; the cache
// draws its outline (HarfBuzz draw callbacks into an openmui-raster path) and
// rasterises it with openmui-raster, so text pixels are as deterministic as
// any other shape. Colour glyphs (COLR layers, as emoji fonts have) become
// premultiplied images composed from the font's palette.
export module openmui.text:glyphs;

import std;
import openmui.geom;
import openmui.raster;
import openmui.render;
import :font;

export namespace omui::text {

class GlyphCache final : public render::GlyphSource {
public:
    explicit GlyphCache(FontCollection& fonts);
    ~GlyphCache() override;

    render::GlyphMask mask(render::FontRef font, std::uint32_t glyph, float size, geom::Point fraction) override;
    raster::PathData outline(render::FontRef font, std::uint32_t glyph) override;

    [[nodiscard]] std::size_t size() const noexcept;  // cached entries
    void clear();

    // Horizontal phases per pixel (4: positions snap to quarter pixels).
    static constexpr int phases = 4;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::text
