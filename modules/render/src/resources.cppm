// openmui.render:resources — what a display list refers to by id.
//
// Images live in an ImageStore (premultiplied pixels, shared, immutable);
// glyphs come from a GlyphSource the text engine implements, so this package
// renders text without knowing fonts.
export module openmui.render:resources;

import std;
import openmui.geom;
import openmui.raster;
import :display;

export namespace omui::render {

class ImageStore {
public:
    // Takes premultiplied RGBA pixels; returns the reference a display list uses.
    ImageRef add(int width, int height, std::vector<raster::Rgba8> pixels);
    ImageRef add(std::shared_ptr<const raster::Surface> surface);
    void remove(ImageRef image);
    [[nodiscard]] const raster::Surface* get(ImageRef image) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }

private:
    std::unordered_map<std::uint32_t, std::shared_ptr<const raster::Surface>> images_;
    std::uint32_t next_ = 1;
};

// A glyph's coverage at a size, positioned relative to its origin on the
// baseline: the mask's bounds are in device pixels with (0, 0) at the origin.
struct GlyphMask {
    const raster::Coverage* coverage = nullptr;
};

// Implemented by the text engine. One virtual call per glyph (a coarse
// boundary: masks are cached behind it).
class GlyphSource {
public:
    virtual ~GlyphSource() = default;
    // Coverage of `glyph` at `size` device pixels per em, with the origin's
    // subpixel offset `fraction` (each in [0, 1), quantised by the source).
    [[nodiscard]] virtual GlyphMask mask(FontRef font, std::uint32_t glyph, float size, Point fraction) = 0;
    // The outline in em units, y down, for transforms masks cannot express.
    [[nodiscard]] virtual raster::PathData outline(FontRef font, std::uint32_t glyph) = 0;
};

struct Resources {
    const ImageStore* images = nullptr;
    GlyphSource*      glyphs = nullptr;
};

}  // namespace omui::render
