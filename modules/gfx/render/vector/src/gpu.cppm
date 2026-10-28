// openmui.render:gpu — the GPU backend of the renderer (D-33, D-57): a display
// list drawn on an omui.rhi device into a colour target, matching the CPU
// reference within the tolerances its tests state.
//
// One shader (shaders/ui.slang) draws every primitive as an instanced quad.
// What the CPU computes exactly is computed the same way here: an
// axis-aligned rectangle's coverage is the area of the pixel inside it,
// gradients are looked up in the CPU's own 256-entry tables at the same
// index, and paths, strokes, glyphs, box shadows and rounded clips are
// coverage the CPU rasteriser produces, placed in an atlas. A rounded
// rectangle is covered by the exact area of its circles (the CPU, flattening
// arcs within 1/32 pixel, is within a few levels of it); rotated rectangles
// use signed distances. Layers are
// textures composited when they pop; a clip that is not a device-aligned
// rectangle is a mask texture. Draws are batched in list order, a batch
// breaking only where a texture or the target changes.
//
// A layer under a blend mode other than normal is composited by a second
// shader (shaders/blend.slang) over a copy of what lies under it, with the
// CPU's arithmetic.
//
// External textures are what the program made on the device
// (GpuOptions::external), drawn over their rectangle like an image. Effects
// without a GPU path are counted in effects_skipped. Object ids are the CPU
// renderer's.
export module openmui.render.vector:gpu;

import std;
import openmui.geom;
import openmui.rhi;
import :display;
import :resources;
import :cpu;
export import :ui_shaders;
export import :blend_shaders;

export namespace omui::render {

struct GpuOptions {
    float scale = 1.0f;  // physical pixels per logical pixel
    bool  clear = true;
    Color background = Color::transparent();
    // The textures external_texture() commands name (OMUI_EXT_external_texture):
    // what the program made on the same device — a video frame it decoded,
    // an engine's view — premultiplied, sampled. None for an id: skipped and
    // counted, as the CPU renderer counts every one.
    std::function<std::optional<rhi::Texture>(std::uint64_t id)> external;
};

class GpuRenderer {
public:
    // The renderer's pipelines and textures on `device`; nullopt with why
    // when the device cannot take them (its shader formats).
    [[nodiscard]] static rhi::Result<GpuRenderer> create(rhi::Device& device);

    // Draws `list` into `target` (an rgba8_unorm colour target) and submits
    // the work; read the target back, or present it, after.
    [[nodiscard]] rhi::Result<RenderReport> render(const DisplayList& list, rhi::Texture target, const Resources& resources,
                                                   const GpuOptions& options = {});

    [[nodiscard]] rhi::Device& device() const noexcept;

    GpuRenderer(GpuRenderer&&) noexcept;
    GpuRenderer& operator=(GpuRenderer&&) noexcept;
    ~GpuRenderer();

private:
    struct Impl;
    explicit GpuRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The shader formats the renderer's shaders were compiled to by this build:
// a device must take one.
[[nodiscard]] rhi::ShaderFormats gpu_shader_formats() noexcept;

}  // namespace omui::render
