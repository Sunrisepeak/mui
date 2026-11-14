// openmui.render:resources — what a display list refers to by id.
//
// Images live in an ImageStore (premultiplied pixels, shared, immutable);
// glyphs come from a GlyphSource the text engine implements, so this package
// renders text without knowing fonts.
export module xrender.vector.api:resources;

import std;
import mlibs.geom;
import xrender.raster;
import :display;

export namespace xrender {

class ImageStore {
public:
    // Takes premultiplied RGBA pixels; returns the reference a display list uses.
    ImageRef add(int width, int height, std::vector<xrender::raster::Rgba8> pixels);
    ImageRef add(std::shared_ptr<const xrender::raster::Surface> surface);
    void remove(ImageRef image);
    [[nodiscard]] const xrender::raster::Surface* get(ImageRef image) const noexcept;
    // The image itself, shared: work on another thread keeps it alive and
    // never reads the store.
    [[nodiscard]] std::shared_ptr<const xrender::raster::Surface> shared(ImageRef image) const;
    // An image under an id another store gave it, at that store's revision
    // of it: a snapshot of what a display list refers to, for work on
    // another thread.
    void put(ImageRef image, std::shared_ptr<const xrender::raster::Surface> surface, std::uint64_t revision = 0);
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }

    // An image that changes in place (a simulated field, a video frame,
    // D-132): new pixels of the same size under the same id, with the
    // rectangles that changed (none given: all of it). Its revision goes up;
    // a renderer holding an older one redraws (CPU damage) and re-uploads
    // (GPU) only those rectangles. False when the image is not in the store
    // or the size differs.
    bool update(ImageRef image, std::shared_ptr<const xrender::raster::Surface> surface,
                std::span<const mlibs::geom::PixelRect> changed = {});
    // How many times it was updated (0: never, or not in the store).
    [[nodiscard]] std::uint64_t revision(ImageRef image) const noexcept;
    // Appends the rectangles changed after `revision` to `out`; false when the
    // store no longer remembers back that far (treat all of it as changed).
    // The last `remembered` updates of an image are kept.
    static constexpr std::size_t remembered = 8;
    [[nodiscard]] bool changed_since(ImageRef image, std::uint64_t revision, std::vector<mlibs::geom::PixelRect>& out) const;

private:
    struct Updates {
        std::uint64_t revision = 0;
        std::deque<std::pair<std::uint64_t, std::vector<mlibs::geom::PixelRect>>> recent;  // (revision, its rectangles)
    };
    std::unordered_map<std::uint32_t, std::shared_ptr<const xrender::raster::Surface>> images_;
    std::unordered_map<std::uint32_t, Updates> updates_;
    std::uint32_t next_ = 1;
};

// A glyph at a size, positioned relative to its origin on the baseline: the
// bounds are in device pixels with (0, 0) at the origin. A colour glyph (an
// emoji) has premultiplied pixels instead of a coverage mask and ignores the
// run's paint.
struct GlyphMask {
    const xrender::raster::Coverage* coverage = nullptr;
    const xrender::raster::Surface*  color = nullptr;
    mlibs::geom::PixelRect         color_bounds;
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
    [[nodiscard]] virtual xrender::raster::PathData outline(FontRef font, std::uint32_t glyph) = 0;
};

struct Resources {
    const ImageStore* images = nullptr;
    GlyphSource*      glyphs = nullptr;
};

}  // namespace xrender
