// openmui.render:cpu — the CPU reference backend: display lists to pixels with
// openmui-raster, byte-for-byte the same on every platform (spec R-2).
//
// Effects (shader parameters on layers) are a GPU capability; this backend
// draws the layer without its effect and counts it in the report (R-3).
// External textures are drawn as nothing and counted the same way.
export module openmui.render:cpu;

import std;
import openmui.geom;
import openmui.raster;
import :display;
import :resources;

export namespace omui::render {

struct RenderReport {
    std::size_t commands = 0;
    std::size_t effects_skipped = 0;
    std::size_t external_skipped = 0;
    std::size_t missing_images = 0;
    std::size_t missing_glyphs = 0;
};

struct CpuOptions {
    float scale = 1.0f;        // physical pixels per logical pixel
    bool  object_ids = false;  // fill the object-id buffer (pixel provenance)
    bool  clear = true;
    Color background = Color::transparent();
};

class CpuRenderer {
public:
    CpuRenderer() = default;

    RenderReport render(const DisplayList& list, raster::Surface& target, const Resources& resources,
                        const CpuOptions& options = {});

    // After a render with object_ids: the node that drew each pixel (0: none).
    [[nodiscard]] std::span<const std::uint32_t> object_ids() const noexcept { return ids_; }
    [[nodiscard]] std::uint32_t object_at(int x, int y, int width) const noexcept {
        std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
        return i < ids_.size() ? ids_[i] : 0;
    }

private:
    std::vector<std::uint32_t> ids_;
};

}  // namespace omui::render
