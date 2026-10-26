// openmui.render:cpu — the CPU reference backend: display lists to pixels with
// openmui-raster, byte-for-byte the same on every platform (spec R-2).
//
// Effects (shader parameters on layers) are a GPU capability; this backend
// draws the layer without its effect and counts it in the report (R-3).
// External textures are drawn as nothing and counted the same way.
//
// Given bands of rows (CpuOptions::rows, from a DamageTracker), it draws only
// those rows of a surface that still holds the previous frame, and the result
// is byte-for-byte the full render (raster::Canvas::restrict_rows). The
// canvas and its buffers live as long as the renderer: a frame of the same
// shape as the last allocates nothing.
export module openmui.render:cpu;

import std;
import openmui.geom;
import openmui.raster;
import :display;
import :resources;
import :damage;

export namespace omui::render {

struct RenderReport {
    std::size_t commands = 0;
    std::size_t effects_skipped = 0;
    std::size_t external_skipped = 0;
    std::size_t missing_images = 0;
    std::size_t missing_glyphs = 0;
    std::size_t rows = 0;      // rows drawn
    std::size_t skipped = 0;   // draw commands outside every band
};

struct CpuOptions {
    float scale = 1.0f;        // physical pixels per logical pixel
    bool  object_ids = false;  // fill the object-id buffer (pixel provenance)
    bool  clear = true;
    Color background = Color::transparent();
    // Only these rows (sorted, not overlapping), over what the surface holds;
    // empty: the whole surface.
    std::span<const RowSpan> rows;
};

class CpuRenderer {
public:
    CpuRenderer();

    RenderReport render(const DisplayList& list, raster::Surface& target, const Resources& resources,
                        const CpuOptions& options = {});

    CpuRenderer(CpuRenderer&&) noexcept;
    CpuRenderer& operator=(CpuRenderer&&) noexcept;
    ~CpuRenderer();

    // After a render with object_ids: the node that drew each pixel (0: none);
    // a partial render updates its rows and keeps the others.
    [[nodiscard]] std::span<const std::uint32_t> object_ids() const noexcept;
    [[nodiscard]] std::uint32_t object_at(int x, int y, int width) const noexcept {
        const auto ids = object_ids();
        std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        return i < ids.size() ? ids[i] : 0;
    }

private:
    struct Scratch;
    void draw(const DisplayList& list, raster::Surface& target, const Resources& resources, const CpuOptions& options, RowSpan band, bool whole,
              RenderReport& report);
    std::unique_ptr<Scratch> scratch_;
    bool                     ids_on_ = false;
};

}  // namespace omui::render
