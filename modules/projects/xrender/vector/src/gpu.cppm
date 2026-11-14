// openmui.render:gpu — the GPU backend of the renderer (D-33, D-57): a display
// list drawn on an omui.rhi device into a colour target, matching the CPU
// reference within the tolerances its tests state.
//
// One shader (shaders/ui.slang) draws every primitive as an instanced quad.
// What the CPU computes exactly is computed the same way here: an
// axis-aligned rectangle's coverage is the area of the pixel inside it,
// gradients are looked up in the CPU's own 256-entry tables at the same
// index, and paths, strokes, glyphs, box shadows and rounded clips are
// coverage the CPU rasteriser produces, placed in an atlas (a glyph in motion
// may be covered from its outline instead: GpuOptions::glyph_masks). A rounded
// rectangle is covered by the exact area of its circles (the CPU, flattening
// arcs within 1/32 pixel, is within a few levels of it); rotated rectangles
// use signed distances. Layers are
// textures composited when they pop; a clip that is not a device-aligned
// rectangle is a mask texture. Draws are batched in list order, a batch
// breaking only where a texture or the target changes.
//
// An image lit by a Material is drawn by the same shader's second fragment
// entry (ui_lit_fs): its normal map beside the image, the material's
// constants as raster's material_setup makes them in a uniform buffer, so
// the shading is the CPU's arithmetic; the pipeline switches only where a
// run of lit draws begins or ends.
//
// A layer under a blend mode other than normal is composited by a second
// shader (shaders/blend.slang) over a copy of what lies under it, with the
// CPU's arithmetic.
//
// External textures are what the program made on the device
// (GpuOptions::external), drawn over their rectangle like an image. Effects
// without a GPU path are counted in effects_skipped. Object ids are the CPU
// renderer's.
export module xrender.vector.api:gpu;

import std;
import mlibs.geom;
import mlibs.rhi;
import :display;
import :resources;
import :cpu;
export import :ui_shaders;
export import :material_shaders;
export import :blend_shaders;

export namespace xrender {

// The glyph masks a frame may make where frames follow one another (a host's
// window, a benchmark of one): none. Text and smoothed corners are their
// outlines' at every frame — each glyph where it is to the fraction of a
// pixel, not at a cache's quarter — so a scale that changes rasterises
// nothing, and nothing is swapped for another picture when it comes to rest:
// a zoom neither stalls nor jumps (GpuOptions::glyph_masks).
inline constexpr std::size_t glyph_masks_a_live_frame = 0;

struct GpuOptions {
    float scale = 1.0f;  // physical pixels per logical pixel
    bool  clear = true;
    Color background = Color::transparent();
    // The textures external_texture() commands name (OMUI_EXT_external_texture):
    // what the program made on the same device — a video frame it decoded,
    // an engine's view — premultiplied, sampled. None for an id: skipped and
    // counted, as the CPU renderer counts every one.
    std::function<std::optional<mlibs::rhi::Texture>(std::uint64_t id)> external;
    // How many glyph masks one frame may rasterise and upload. Unlimited (the
    // default): every glyph by its mask, the CPU's own bytes. Limited (what a
    // host drawing frame after frame gives): a glyph whose mask the atlas has
    // not got is covered on the device from its outline — the exact area of
    // its flattened contours, the same at every size and place, within a few
    // levels of the mask — and takes its mask once it has been asked for at
    // the same size and place in two frames running, this many a frame (0:
    // never — the outlines at rest too, nothing swapped). Text under a
    // changing scale or transform costs no rasterising. A path filled by
    // the non-zero rule is drawn so too while it moves or changes, and takes
    // its mask once it has stayed two frames (a few a frame): where its
    // contours cross, a pixel's area by magnitude is not the rule's, so at
    // rest it is the reference's. RenderReport's glyphs_outlined says how
    // many shapes a frame drew so.
    std::size_t glyph_masks = std::numeric_limits<std::size_t>::max();
    // Effect layers of the frame's own surface share one surface, a cell each: three passes for them
    // all, where a surface each is three passes a layer — the same bytes (RenderReport::layers_folded).
    // False: a surface each, what the shared one is checked against.
    bool fold_layers = true;
    // Where no glyph mask is made (glyph_masks 0), the glyphs no kept run holds go as the frame's own
    // run: a small instance each (a third of a primitive's bytes) drawn as a kept run's are, the commands
    // that follow one another under one clip in one draw — the same bytes (RenderReport::glyphs_framed).
    // False: a primitive each, what the run is checked against.
    bool frame_glyphs = true;
};

class GpuRenderer {
public:
    // The renderer's pipelines and textures on `device`; nullopt with why
    // when the device cannot take them (its shader formats).
    [[nodiscard]] static mlibs::rhi::Result<GpuRenderer> create(mlibs::rhi::Device& device);

    // Draws `list` into `target` (an rgba8_unorm colour target) and submits
    // the work; read the target back, or present it, after.
    [[nodiscard]] mlibs::rhi::Result<RenderReport> render(const DisplayList& list, mlibs::rhi::Texture target, const Resources& resources,
                                                   const GpuOptions& options = {});

    [[nodiscard]] mlibs::rhi::Device& device() const noexcept;

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
[[nodiscard]] mlibs::rhi::ShaderFormats gpu_shader_formats() noexcept;

}  // namespace xrender
