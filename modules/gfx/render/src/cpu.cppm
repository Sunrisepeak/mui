// openmui.render:cpu — the CPU reference backend: display lists to pixels with
// openmui.raster, byte-for-byte the same on every platform (spec R-2).
//
// Effects (D-68): a layer with an effect from render::effects() is drawn
// offscreen first — whole, whatever bands are asked for, since an effect may
// move pixels across rows — the effect's CPU reference runs on it, and the
// result is composited where the layer is. A layer whose effect is not
// registered is drawn without it and counted in the report (R-3). External
// textures are drawn as nothing and counted the same way.
//
// Given bands of rows (CpuOptions::rows, from a DamageTracker), it draws only
// those rows of a surface that still holds the previous frame, and the result
// is byte-for-byte the full render (raster::Canvas::restrict_rows). The
// canvas and its buffers live as long as the renderer: a frame of the same
// shape as the last allocates nothing.
export module openmui.render:cpu;

import std;
import openmui.geom;
import openmui.memory;
import openmui.raster;
import :display;
import :resources;
import :damage;

export namespace omui::render {

struct RenderReport {
    std::size_t commands = 0;
    std::size_t effects_skipped = 0;
    std::size_t uploaded = 0;  // GPU: bytes the frame sent to the device (images once, coverage, instances)
    std::size_t external_skipped = 0;
    std::size_t missing_images = 0;
    std::size_t missing_glyphs = 0;
    std::size_t rows = 0;      // rows drawn
    std::size_t skipped = 0;   // draw commands outside every band
    // GPU: coverage masks (glyphs, paths, shadows) rasterised and uploaded
    // this frame, and drawn from the atlas as an earlier frame left them;
    // `atlas_emptied` when the frame was encoded again with the atlas emptied.
    std::size_t masks_made = 0;
    std::size_t masks_reused = 0;
    bool        atlas_emptied = false;
    // With CpuOptions::measure: per kind of command (the index of Command's
    // alternative, command_kind_name), how many and how long — where a
    // frame's raster time goes.
    struct Kind {
        std::size_t count = 0;
        double      ms = 0;
    };
    std::array<Kind, std::variant_size_v<Command>> kinds{};
};

// The name of a command kind: "fill_rect", "draw_glyphs" …
[[nodiscard]] std::string_view command_kind_name(std::size_t index) noexcept;

// Runs body(0) … body(count − 1) at the same time, in any order, and returns
// when all have run: the host's thread pool (openmui.core's Tasks, or any
// other). This package has none of its own.
class Parallel {
public:
    virtual void run(std::size_t count, FunctionRef<void(std::size_t)> body) = 0;

protected:
    ~Parallel() = default;
};

struct CpuOptions {
    float scale = 1.0f;        // physical pixels per logical pixel
    bool  object_ids = false;  // fill the object-id buffer (pixel provenance)
    bool  clear = true;
    Color background = Color::transparent();
    // Only these rows (sorted, not overlapping), over what the surface holds;
    // empty: the whole surface.
    std::span<const RowSpan> rows;
    // Bands of at most `rows_per_task` rows drawn at the same time. The bytes
    // are those of a serial render: every band is exact (restrict_rows).
    Parallel*    parallel = nullptr;
    std::int32_t rows_per_task = 64;
    bool         measure = false;  // fill RenderReport::kinds (a clock read per command)
    // The device transform the list starts under (default: scale). An
    // effect's layer renders offscreen under its own (D-68).
    std::optional<Affine> transform;
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
    struct Impl;
    void draw(Scratch& scratch, const DisplayList& list, raster::Surface& target, const Resources& resources, const CpuOptions& options,
              RowSpan band, bool whole, RenderReport& report);
    void prepare_effects(const DisplayList& list, const raster::Surface& target, const Resources& resources, const CpuOptions& options,
                         RenderReport& report);
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::render
