module xrender.vector.api;

import std;
import mlibs.geom;
import xrender.raster;
import mlibs.rhi;
import :shared;

namespace xrender {

namespace {

namespace ui = shaders::ui;
namespace material = shaders::material;
namespace blend = shaders::blend;

// One instance of the shader's quad (shaders/ui.slang, struct Prim).
struct Prim {
    std::array<float, 4> rect{}, m0{}, m1{}, radii{}, shape{}, color{}, paint{}, pgeo{}, pinv0{}, pinv1{}, clip{}, uv{};
};
static_assert(sizeof(Prim) == 12 * 16);

constexpr float kind_rect = 0, kind_rrect = 1, kind_mask = 2, kind_image = 3, kind_rring = 4, kind_glyph = 5, kind_shadow = 6;
constexpr int flag_axis = 1, flag_clip_mask = 2, flag_linear = 4, flag_layer = 8;
constexpr int clip_record_shift = 8;  // above the flags: the record of the clip covered from its outline (0: none)

// A value 0…1 as a byte, the way raster rounds opacities.
float byte_of(float v) { return static_cast<float>(static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); }

void edges(std::array<float, 4>& v, const Rect& r) { v = {r.x, r.y, r.right(), r.bottom()}; }

struct Draw {
    Prim         prim;
    mlibs::rhi::Texture image;   // the image slot's texture (none: white)
    mlibs::rhi::Texture mask;    // the clip mask (none: white)
    mlibs::rhi::Texture normal;  // a lit image's normal map
    std::int32_t lit = -1;  // a lit image: its material's uniforms (Impl::lits), drawn by ui_lit_fs
    std::int32_t kept = -1; // glyphs kept on the device: which of the frame's (Impl::kept_draws); its prim is not drawn
};

// ui.slang's KeptGlyph: a glyph in the space its run was drawn in.
struct KeptGlyph {
    std::array<float, 4> across{}, origin{}, bounds{}, color{};
};
static_assert(sizeof(KeptGlyph) == 4 * 16);

// ui.slang's Kept: where a run's space lies this frame, and the clip.
struct KeptUniforms {
    std::array<float, 4> across{}, origin{}, clip{};
    friend bool operator==(const KeptUniforms&, const KeptUniforms&) = default;
};

// ui.slang's Lit: a material's constants, as raster's MaterialSetup has them.
struct LitUniforms {
    std::array<float, 4> light, face, shadow, rim, nsize, key;
    friend bool operator==(const LitUniforms&, const LitUniforms&) = default;
};

LitUniforms lit_uniforms(const xrender::raster::MaterialSetup& m, float normal_w, float normal_h) {
    return {{m.lx, m.ly, m.lz, static_cast<float>(m.bands)},
            {m.fx, m.fy, m.inv_softness, m.rim_width},
            {m.shadow_r, m.shadow_g, m.shadow_b, m.shadow_strength},
            {m.rim_r, m.rim_g, m.rim_b, m.rim_strength},
            {normal_w, normal_h, m.inv_levels, m.inv_steps},
            {m.key_r, m.key_g, m.key_b, 0}};
}

// A layer composited under a blend mode other than normal (shaders/
// blend.slang): the target's box is copied to the backdrop, and the layer
// blended with it is written in its place.
struct BlendPass {
    mlibs::rhi::Texture         layer;
    mlibs::rhi::Texture         backdrop;
    mlibs::rhi::Region          box;
    BlendMode            mode = BlendMode::normal;
    std::uint32_t        opacity = 255;  // 0…255, as the CPU rounds it
    mlibs::rhi::Texture         mask;           // the clip mask, or none
    std::array<float, 4> clip{};         // the clip rectangle
};

// What blend.slang's uniforms are.
struct BlendParams {
    std::array<float, 4> size, box, mode, clip;
};

// What the effect template's uniforms are (effects/lib/openmui_effect.slang).
struct EffectUniforms {
    std::array<float, 4>  size{}, layer{};
    std::array<float, 16> params{};
    std::array<float, 8>  derived{};
    std::array<float, 4>  shape_info{};  // the number of shapes
    std::array<float, 64> shapes{};      // 8 × (x, y, w, h; tl, tr, br, bl)
};
static_assert(sizeof(EffectUniforms) == 100 * 4);
constexpr std::size_t max_shapes = 8;

// pixelate.slang's uniforms.
struct PixelateGrid {
    std::array<std::int32_t, 4> size, first;
};

// An effect run on a layer's surface (D-68): `source` holds the layer drawn
// offscreen, `result` what the effect makes of it.
struct EffectStep {
    EffectGpu             gpu;
    mlibs::rhi::GraphicsPipeline pipeline;  // pixel: the effect's
    EffectUniforms        uniforms;
    mlibs::rhi::Texture          source, scratch, result, aux;
    // An effect that reads its backdrop: what the parent target holds under
    // the surface — the same when the layer ends as when it began, since its
    // content is drawn elsewhere — copied into `aux` and frosted.
    bool                  backdrop = false;
    mlibs::rhi::Texture          parent;
    std::int32_t          x0 = 0, y0 = 0;  // the surface's corner in the parent
    std::uint32_t         parent_w = 0, parent_h = 0;
    int                   frost = 0;       // box radius, pixels
};

// The frame's work, in the order it runs: a layer's passes run before the
// rest of its parent's.
struct Pass {
    enum class Kind : std::uint8_t { draws, blend, effect };
    Kind                 kind = Kind::draws;
    mlibs::rhi::Texture         target;
    std::uint32_t        w = 0, h = 0;
    mlibs::rhi::LoadOp          load = mlibs::rhi::LoadOp::load;
    std::array<float, 4> clear{};
    std::vector<Draw>    draws;   // draws
    bool material_mix = false;   // rgba16_float additive leaf fields, no coverage
    BlendPass            blend;   // blend
    EffectStep           effect;  // effect
};

struct Frame {
    std::array<float, 4> size;   // target w, h; atlas w, h
    std::array<float, 4> ramps;  // ramp rows
};

struct Clip {
    float        x0, y0, x1, y1;  // the device rectangle
    mlibs::rhi::Texture mask;           // the mask texture, or none
    std::shared_ptr<std::vector<std::uint8_t>> coverage;  // the mask, surface-sized
    std::uint64_t key = 0;       // what made the mask (0: none)
    // A rounded clip covered on the device from its outline (D-188): its record in the frame's (from 1; 0: none),
    // how many such lie under one another here, and the shape it is (the frame's clip_shapes; -1: none).
    std::uint32_t record = 0;
    int           depth = 0;
    int           shape = -1;
};

// A rounded clip a record covers: the rectangle, under what transform, and the one it lies within.
struct ClipShape {
    Affine      to;
    Rect        rect;
    CornerRadii radii;
    int         below = -1;
};

// A colour premultiplied to bytes as raster does it, as exact fractions of 255.
std::array<float, 4> premultiplied(Color c) {
    const xrender::raster::Rgba8 b = xrender::raster::premultiply(c);
    return {b.r / 255.0f, b.g / 255.0f, b.b / 255.0f, b.a / 255.0f};
}

void set(std::array<float, 4>& v, float a, float b, float c, float d) { v = {a, b, c, d}; }

void transform(Prim& p, const Affine& m, float kind, int flags) {
    set(p.m0, m.a, m.b, m.c, m.d);
    set(p.m1, m.e, m.f, kind, static_cast<float>(flags));
}

void inverse(Prim& p, const Affine& m) {
    const Affine inv = m.inverse().value_or(Affine{});
    set(p.pinv0, inv.a, inv.b, inv.c, inv.d);
    p.pinv1[0] = inv.e;
    p.pinv1[1] = inv.f;
}

xrender::raster::PixelRect pixels(const Clip& c) {
    const auto x0 = static_cast<std::int32_t>(std::floor(c.x0)), y0 = static_cast<std::int32_t>(std::floor(c.y0));
    const auto x1 = static_cast<std::int32_t>(std::ceil(c.x1)), y1 = static_cast<std::int32_t>(std::ceil(c.y1));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// What the effect template allows a shader to bind: the source and the aux
// texture, the uniforms; no storage (architecture §8).
bool template_shaped(const mlibs::rhi::ShaderDesc& s) {
    // source, and aux (rows, or the backdrop); the uniforms
    return s.stage == mlibs::rhi::Stage::fragment && s.samplers <= 2 && s.uniform_buffers <= 1 && s.storage_textures == 0 && s.storage_buffers == 0 &&
           s.rw_storage_textures == 0 && s.rw_storage_buffers == 0;
}

// What made a coverage mask, as 64 bits: the same inputs make the same
// bytes, so a mask is rasterised and uploaded once and reused while it is
// drawn (a glyph, a border, a shadow of an interface that did not move).
class Key {
public:
    Key& u64(std::uint64_t v) noexcept {
        h_ = mix(h_ ^ v);
        return *this;
    }
    Key& f32(float v) noexcept { return u64(std::bit_cast<std::uint32_t>(v)); }
    Key& i32(std::int32_t v) noexcept { return u64(static_cast<std::uint32_t>(v)); }
    Key& bytes(std::span<const std::byte> b) noexcept {
        u64(b.size());
        std::size_t i = 0;
        for (; i + 8 <= b.size(); i += 8) {
            std::uint64_t v;
            std::memcpy(&v, b.data() + i, 8);
            u64(v);
        }
        if (i < b.size()) {
            std::uint64_t v = 0;
            std::memcpy(&v, b.data() + i, b.size() - i);
            u64(v);
        }
        return *this;
    }
    Key& rect(const Rect& r) noexcept { return f32(r.x).f32(r.y).f32(r.width).f32(r.height); }
    Key& radii(const CornerRadii& c) noexcept {
        f32(c.top_left).f32(c.top_right).f32(c.bottom_right).f32(c.bottom_left);
        return c.smoothing != 0 ? f32(c.smoothing) : *this;
    }
    Key& affine(const Affine& m) noexcept { return f32(m.a).f32(m.b).f32(m.c).f32(m.d).f32(m.e).f32(m.f); }
    Key& pixels(const xrender::raster::PixelRect& r) noexcept { return i32(r.x).i32(r.y).i32(r.width).i32(r.height); }
    Key& path(const PathView& v) noexcept { return bytes(std::as_bytes(v.verbs)).bytes(std::as_bytes(v.points)); }
    [[nodiscard]] std::uint64_t value() const noexcept { return h_ ? h_ : 1; }

private:
    static std::uint64_t mix(std::uint64_t x) noexcept {
        x *= 0x9E3779B97F4A7C15ull;
        x ^= x >> 32;
        x *= 0xD6E8FEB86659FD93ull;
        x ^= x >> 29;
        return x;
    }
    std::uint64_t h_ = 0x243F6A8885A308D3ull;
};

}  // namespace

struct GpuRenderer::Impl {
    LayerBoundsMap layer_bounds;
    mlibs::rhi::Device&          d;
    mlibs::rhi::GraphicsPipeline pipeline, blend_pipeline;
    mlibs::rhi::GraphicsPipeline lit_pipeline;  // ui_lit_fs: images lit by a material
    mlibs::rhi::GraphicsPipeline kept_pipeline; // ui_kept_vs: glyphs kept on the device
    std::array<mlibs::rhi::VertexAttribute, 4>    kept_attributes{};
    std::array<mlibs::rhi::VertexBufferLayout, 1> kept_layout{};
    // A display list's kept run (DisplayList::append with an id), its glyphs on the device: an instance a
    // glyph in the space of the command that drew it, made when the run is first met at its revision and
    // again when the scale leaves what its outlines' levels were chosen for.
    struct KeptPart {
        std::size_t   command = 0;       // within the run
        std::uint32_t first = 0, count = 0;
        bool          kept = false;      // no: the command is encoded each frame
    };
    struct KeptRun {
        std::uint64_t         revision = 0;
        mlibs::rhi::Buffer    buffer;
        std::uint32_t         capacity = 0;  // bytes
        std::vector<KeptPart> parts;         // a DrawGlyphs each, in order
        float                 scale = 0;     // the run's transform's when made
        float                 least = 0, most = 0;  // the scales, by that one, its outlines serve
        std::uint64_t         last = 0;
        bool                  made = false;
    };
    std::unordered_map<std::uint64_t, KeptRun> kept_runs;
    std::vector<KeptGlyph>                     kept_staging;
    struct KeptDraw {
        const KeptRun* run = nullptr;
        std::uint32_t  first = 0, count = 0;
        KeptUniforms   uniforms;
    };
    std::vector<KeptDraw> kept_draws;  // the frame's
    mlibs::rhi::GraphicsPipeline material_pipeline, material_lit_pipeline; // lazy rgba16_float accumulation
    mlibs::rhi::Sampler          nearest;  // every texture is read texel by texel; bilinear images are weighed in the shader
    mlibs::rhi::Texture          white, white_mask, zero_row;
    mlibs::rhi::Texture          ramps;
    std::uint32_t         ramp_capacity = 0;
    // The coverage atlas, kept from frame to frame: masks by what made them
    // (Key), placed on shelves 2048 wide; it grows to the device's limit, and
    // when even that is full it is emptied and the frame encoded again.
    mlibs::rhi::Texture          atlas;
    std::uint32_t         atlas_w = 4096, atlas_h = 0;  // a 4K target's widest mask fits
    struct Cached {
        std::uint32_t     x = 0, y = 0;        // in the atlas
        xrender::raster::PixelRect bounds;              // where the mask lies, as rasterised
        bool              color = false;       // a colour glyph: no mask, its image instead
        std::uint64_t     last = 0;
    };
    std::unordered_map<std::uint64_t, Cached> cached;
    std::uint32_t         shelf_x = 0, shelf_y = 0, shelf_h = 0;
    bool                  atlas_overflow = false;  // this encoding ran out of atlas
    // Grown freely to this many rows (8 MB); past it the atlas is first
    // emptied of what earlier frames left (the frame encoded again), and
    // grows on only for what one frame draws.
    static constexpr std::uint32_t atlas_soft_rows = 2048;
    bool                  atlas_second_pass = false;
    std::size_t           masks_made = 0, masks_reused = 0;  // this encoding's
    std::vector<std::vector<Draw>> draw_lists;  // the last frame's passes' draws, emptied, for this frame's
    // Glyphs' outlines for the device (GpuOptions::glyph_masks, the shader's
    // KIND_GLYPH): each one's contours flattened once a level of size — as
    // finely as the CPU flattens at the level's largest — and kept in a
    // texture of floats, two a point (ems), 1024 points a row; an x of
    // `contour_begins` starts a contour.
    struct Outlined {
        std::uint32_t first = 0, count = 0;  // points
        Rect          bounds;                // ems
        bool          blank = false;         // no outline and no image (a space): nothing to draw
    };
    static constexpr std::uint32_t outline_w = 2048;
    static constexpr float         contour_begins = 1e30f;
    static constexpr float         outline_largest = 512;  // pixels an em: past it a glyph is its mask
    static constexpr std::size_t   outline_most = std::size_t{outline_w} * 2048;  // floats: past it the store begins again
    // The store's first rows are the frame's clips: a record of seven points a rounded clip covered from its
    // outline — its transform (a, b; c, d; e, f), its outline (first point, points), the plain part (a device
    // rectangle's corners; the reach of its corners, below 0 for none) and the record of the clip it lies within.
    static constexpr std::uint32_t clip_rows = 8;
    static constexpr std::size_t   clip_floats = 14;
    static constexpr std::size_t   outline_base = std::size_t{clip_rows} * outline_w;  // floats before the outlines
    static constexpr int           clip_depth_most = 4;  // records one within another (the shader's loop)
    std::vector<float>             clip_records;         // this frame's
    std::vector<ClipShape>         clip_shapes;
    mlibs::rhi::Texture   outlines;
    std::uint32_t         outline_rows = 0;         // the texture's
    std::vector<float>    outline_points = std::vector<float>(outline_base, 0.0f);  // what it holds, whole rows
    std::size_t           outline_used = outline_base;  // floats written
    std::size_t           outline_sent = 0;         // floats the texture has
    std::unordered_map<std::uint64_t, Outlined> outlined;  // by font, glyph, level; none of points: not to be drawn so
    struct Recent {
        std::uint64_t   font = 0;
        std::uint32_t   glyph = 0;
        float           top = 0;  // 0: empty
        const Outlined* outline = nullptr;
    };
    std::unordered_map<std::uint64_t, bool> coloured;  // by font and glyph: a colour glyph (its image, not an outline)
    std::array<Recent, 4096> recents{};  // outline_of's last answers (what `outlined` holds stays where it is)
    xrender::raster::Polygons outline_scratch;
    // The masks asked for and not in the atlas, this frame and (sorted) the
    // one before: a mask is made for what was asked twice running.
    std::vector<std::uint64_t> asked, asked_before;
    std::size_t           glyph_masks = 0, glyph_masks_made = 0, glyphs_outlined = 0;

    std::optional<xrender::raster::PixelRect> too_wide;     // a mask wider than the atlas (an error)
    std::vector<mlibs::rhi::Texture> retired;             // atlases grown out of, destroyed after the submission
    // Rounded and path clips' masks, surface-sized, by what made them.
    struct ClipMask {
        mlibs::rhi::Texture                               texture;
        std::shared_ptr<std::vector<std::uint8_t>> coverage;
        std::uint64_t                              last = 0;
    };
    std::unordered_map<std::uint64_t, ClipMask> clip_masks;
    mlibs::rhi::Buffer           instances;
    std::uint32_t         instance_capacity = 0;
    std::unordered_map<const xrender::raster::Surface*, mlibs::rhi::Texture> images;  // surfaces without an id (glyph colour images)
    // The ImageStore's images by id: immutable while the store holds them, so
    // uploaded once (a wall of thumbnails costs its uploads once, not a frame).
    struct Stored {
        mlibs::rhi::Texture           texture;
        const xrender::raster::Surface* surface = nullptr;
        std::uint64_t                 revision = 0;  // the store's revision of it uploaded
    };
    const ImageStore* store = nullptr;               // this frame's (Resources::images)
    std::vector<mlibs::geom::PixelRect> changed;     // an updated image's rectangles (reused)
    std::unordered_map<std::uint32_t, Stored> stored;
    // Textures of images the store let go, kept by size for the next image
    // of that size (a canvas's tiers come and go every frame of a zoom: no
    // texture made or destroyed for them); one unused for 120 frames goes.
    struct Spare {
        mlibs::rhi::Texture  texture;
        std::uint32_t w, h;
        std::uint64_t last;
    };
    std::vector<Spare> spares;

    // The effects' pipelines, made on first use: a pixel effect's by its ref
    // and generation (a reloaded shader is a new pipeline).
    mlibs::rhi::GraphicsPipeline blur_pipeline, particles_pipeline;
    mlibs::rhi::ComputePipeline  pixelate_pipeline;
    std::map<std::pair<std::uint32_t, std::uint64_t>, mlibs::rhi::GraphicsPipeline> effect_pipelines;

    // The frame's transient textures (layers, backdrops, effect surfaces):
    // the pool's, reused from frame to frame (D-81); the graph the frame's
    // passes are recorded through.
    xrender::TexturePool pool;
    xrender::Graph       graph{pool};
    std::uint64_t       frame_number = 0;

    std::array<mlibs::rhi::VertexAttribute, 12> attributes{};
    std::array<mlibs::rhi::VertexBufferLayout, 1> layout{};

    // The frame being built.
    std::uint32_t                       width = 0, height = 0;
    std::vector<Pass>                   passes;
    std::vector<std::array<xrender::raster::Rgba8, 256>> ramp_rows;
    std::vector<LitUniforms>            lits;     // the frame's materials (Draw::lit)
    xrender::raster::Coverage                    scratch;  // a mask being made (uploaded, then reused)
    std::deque<std::vector<float>>      rows;   // the frame's effect rows (uploads point into them)
    std::vector<const xrender::raster::Surface*> images_used;
    std::vector<Prim>                   prims;  // the frame's instances, all passes'
    mlibs::rhi::CommandList                    list;
    xrender::raster::Polygons                    polygons;
    xrender::raster::Rasterizer                  rasterizer;

    explicit Impl(mlibs::rhi::Device& device) : d(device), pool(device) {
        for (std::uint32_t i = 0; i < 12; ++i) attributes[i] = {i, mlibs::rhi::VertexFormat::float4, i * 16};
        layout[0] = {sizeof(Prim), true, attributes};
        for (std::uint32_t i = 0; i < 4; ++i) kept_attributes[i] = {i, mlibs::rhi::VertexFormat::float4, i * 16};
        kept_layout[0] = {sizeof(KeptGlyph), true, kept_attributes};
    }
    ~Impl() {
        static_cast<void>(d.wait_idle());
        for (auto& [s, t] : images) d.destroy(t);
        for (auto& [id, st] : stored) d.destroy(st.texture);
        for (const Spare& sp : spares) d.destroy(sp.texture);
        for (auto& [k, c] : clip_masks) d.destroy(c.texture);
        for (mlibs::rhi::Texture t : retired) d.destroy(t);
        for (mlibs::rhi::Texture t : {white, white_mask, zero_row, ramps, atlas, outlines}) d.destroy(t);
        d.destroy(instances);
        for (auto& [id, run] : kept_runs)
            if (run.buffer) d.destroy(run.buffer);
        d.destroy(nearest);
        for (mlibs::rhi::GraphicsPipeline p : {kept_pipeline, pipeline, lit_pipeline, blend_pipeline, blur_pipeline, particles_pipeline, material_pipeline, material_lit_pipeline}) d.destroy(p);
        for (auto& [k, p] : effect_pipelines) d.destroy(p);
        d.destroy(pixelate_pipeline);
    }

    mlibs::rhi::Result<void> material_pipelines() {
        if (material_pipeline && material_lit_pipeline) return {};
        auto plain = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &material::ui_material_fs,
            .vertex_buffers = layout, .target = mlibs::rhi::Format::rgba16_float, .blend = mlibs::rhi::Blend::additive(), .label = "render.material.mix"});
        if (!plain) return std::unexpected(plain.error());
        auto lit = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &material::ui_material_lit_fs,
            .vertex_buffers = layout, .target = mlibs::rhi::Format::rgba16_float, .blend = mlibs::rhi::Blend::additive(), .label = "render.material.mix.lit"});
        if (!lit) { d.destroy(*plain); return std::unexpected(lit.error()); }
        material_pipeline = *plain; material_lit_pipeline = *lit;
        return {};
    }

    mlibs::rhi::Result<void> init() {
        auto p = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &ui::ui_fs, .vertex_buffers = layout,
                                                    .blend = mlibs::rhi::Blend::premultiplied(), .label = "render.ui"});
        if (!p) return std::unexpected(p.error());
        pipeline = *p;
        auto lp = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &ui::ui_lit_fs, .vertex_buffers = layout,
                                                     .blend = mlibs::rhi::Blend::premultiplied(), .label = "render.ui.lit"});
        if (!lp) return std::unexpected(lp.error());
        lit_pipeline = *lp;
        auto kp = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &ui::ui_kept_vs, .fragment = &ui::ui_fs, .vertex_buffers = kept_layout,
                                                     .blend = mlibs::rhi::Blend::premultiplied(), .label = "render.ui.kept"});
        if (!kp) return std::unexpected(kp.error());
        kept_pipeline = *kp;
        auto bp = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &blend::blend_vs, .fragment = &blend::blend_fs, .blend = mlibs::rhi::Blend::replace(),
                                                     .label = "render.blend"});
        if (!bp) return std::unexpected(bp.error());
        blend_pipeline = *bp;
        auto n = d.create(mlibs::rhi::SamplerDesc{mlibs::rhi::Filter::nearest, mlibs::rhi::Filter::nearest});
        if (!n) return std::unexpected(n.error());
        nearest = *n;
        auto w = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 1, 1, mlibs::rhi::TextureUsage::sampled, "render.white"});
        auto wm = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, 1, 1, mlibs::rhi::TextureUsage::sampled, "render.white_mask"});
        auto zr = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r32_float, 1, 1, mlibs::rhi::TextureUsage::sampled, "render.zero_row"});
        if (!w || !wm || !zr) return std::unexpected(!w ? w.error() : !wm ? wm.error() : zr.error());
        white = *w;
        white_mask = *wm;
        zero_row = *zr;
        mlibs::rhi::CommandList up;
        const std::array<std::uint8_t, 4> one{255, 255, 255, 255};
        const std::array<float, 1> zero{0};
        up.upload(white, mlibs::rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one)));
        up.upload(white_mask, mlibs::rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one).first(1)));
        up.upload(zero_row, mlibs::rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(zero)));
        auto s = d.submit(up);
        if (!s) return std::unexpected(s.error());
        return d.wait(*s);
    }

    // ── per-frame resources ──────────────────────────────────────────────

    // A texture for this frame only: the graph's, from the pool.
    mlibs::rhi::Result<mlibs::rhi::Texture> pooled(std::uint32_t w, std::uint32_t h, mlibs::rhi::Format format, mlibs::rhi::TextureUsage usage, std::string_view label) {
        return graph.transient({w, h, format, usage}, label);
    }

    // A frame begins: every pooled texture is free again, and one unused for
    // the last 120 frames goes (a size the window no longer has).
    void pool_frame() {
        ++frame_number;
        pool.begin_frame();
        std::erase_if(spares, [&](const Spare& sp) {
            if (frame_number - sp.last <= 120) return false;
            d.destroy(sp.texture);
            return true;
        });
        std::erase_if(clip_masks, [&](auto& kv) {
            if (frame_number - kv.second.last <= 120) return false;
            d.destroy(kv.second.texture);
            return true;
        });
        std::erase_if(kept_runs, [&](auto& kv) {
            if (frame_number - kv.second.last <= 120) return false;
            if (kv.second.buffer) d.destroy(kv.second.buffer);
            return true;
        });
    }

    mlibs::rhi::Result<void> ensure_ramps(std::uint32_t rows) {
        if (rows <= ramp_capacity && ramps) return {};
        std::uint32_t cap = std::max<std::uint32_t>(16, ramp_capacity);
        while (cap < rows) cap *= 2;
        if (ramps) d.destroy(ramps);
        auto t = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, 256, cap, mlibs::rhi::TextureUsage::sampled, "render.ramps"});
        if (!t) return std::unexpected(t.error());
        ramps = *t;
        ramp_capacity = cap;
        return {};
    }

    // The atlas exists (1024 rows to begin with).
    mlibs::rhi::Result<void> ensure_atlas() {
        if (atlas) return {};
        auto t = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, atlas_w, 1024, mlibs::rhi::TextureUsage::sampled, "render.atlas"});
        if (!t) return std::unexpected(t.error());
        atlas = *t;
        atlas_h = 1024;
        return {};
    }

    // The outlines' texture with room for what is kept: made again larger,
    // and sent whole, when it has not.
    mlibs::rhi::Result<void> ensure_outlines() {
        const auto rows = static_cast<std::uint32_t>(std::max<std::size_t>(1, outline_points.size() / outline_w));
        if (outlines && rows <= outline_rows) return {};
        std::uint32_t cap = std::max<std::uint32_t>(64, outline_rows);
        while (cap < rows) cap *= 2;
        auto t = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r32_float, outline_w, cap, mlibs::rhi::TextureUsage::sampled, "render.outlines"});
        if (!t) return std::unexpected(t.error());
        if (outlines) retired.push_back(outlines);
        outlines = *t;
        outline_rows = cap;
        outline_sent = 0;
        return {};
    }

    // Whether a mask the atlas has not got may be made this frame: always
    // when masks are unlimited; else when it was asked for in the frame
    // before too and the frame has made fewer than its number.
    bool may_make(std::uint64_t key) { return may_make(key, glyph_masks, glyph_masks_made); }
    bool may_make(std::uint64_t key, std::size_t most, std::size_t& made) {
        if (glyph_masks == std::numeric_limits<std::size_t>::max()) return true;
        asked.push_back(key);
        if (made < most && std::ranges::binary_search(asked_before, key)) {
            ++made;
            return true;
        }
        return false;
    }
    // A filled path, where a frame's glyph masks are limited: its outline
    // while it moves or changes, its mask — the reference's bytes, whatever
    // its contours cross — once it has stayed two frames, so many a frame.
    static constexpr std::size_t path_masks_a_frame = 16;
    // What one path's outline may cost the device: its points read at every
    // pixel of its box. Past it the path is its mask (until outlines are
    // read by bands of rows).
    static constexpr float outline_reads_most = 3.0e6f;
    std::size_t path_masks_made = 0;

    // A transform's larger scale (Affine::max_scale: a square root by steps), kept for the next of the same
    // linear part — a run's glyphs, a list's rectangles.
    std::array<float, 4> scaled_by{};
    float                scaled = 0;
    float scale_of(const Affine& to) {
        if (to.a != scaled_by[0] || to.b != scaled_by[1] || to.c != scaled_by[2] || to.d != scaled_by[3] || scaled == 0) {
            scaled_by = {to.a, to.b, to.c, to.d};
            scaled = to.max_scale();
        }
        return scaled;
    }

    // The level of size an outline is kept at for `scale` device pixels a
    // unit of its own: the power of two at or past it, flattened as finely as
    // the CPU flattens there (1/32 pixel), so finer below it.
    static float level_top(float scale) {
        float top = 1.0f / 256;
        while (top < scale) top *= 2;
        return top;
    }

    // Contours kept for the device under `key`: `path`'s, and `hole`'s wound
    // the other way (a ring: what the first covers less what the second
    // does), flattened at `top` pixels a unit. No points: nothing to keep.
    Outlined& outline_kept(std::uint64_t key, xrender::raster::PathData path, xrender::raster::PathData hole, float top) {
        Outlined made;
        const std::size_t begun = outline_used;
        auto put = [&](float x, float y) {
            if (outline_used + 2 > outline_points.size()) outline_points.resize(outline_points.size() + outline_w, 0.0f);
            outline_points[outline_used++] = x;
            outline_points[outline_used++] = y;
        };
        float x0 = std::numeric_limits<float>::max(), y0 = x0, x1 = -x0, y1 = -x0;
        auto contours = [&](xrender::raster::PathData from, bool turned) {
            if (from.verbs.empty()) return;
            outline_scratch.clear();
            xrender::raster::flatten(from, Affine::scale(top, top), outline_scratch);
            for (std::size_t polygon = 0; polygon < outline_scratch.count(); ++polygon) {
                const std::span<const Point> points = outline_scratch.polygon(polygon);
                if (points.size() < 3) continue;
                put(contour_begins, 0);
                for (std::size_t index = 0; index <= points.size(); ++index) {
                    const std::size_t from_first = index % points.size();
                    const Point& point = points[turned ? points.size() - 1 - from_first : from_first];
                    const float x = point.x / top, y = point.y / top;
                    put(x, y);
                    x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                }
            }
        };
        contours(path, false);
        contours(hole, true);
        if (outline_used > begun) {
            made.first = static_cast<std::uint32_t>(begun / 2);
            made.count = static_cast<std::uint32_t>((outline_used - begun) / 2);
            made.bounds = {x0, y0, x1 - x0, y1 - y0};
        }
        return outlined.emplace(key, made).first->second;
    }

    // A glyph's outline for the device at `scale` pixels an em; none when it
    // is not to be drawn so: a colour glyph, a scale past the largest level.
    // One of no points and `blank` is drawn, as nothing.
    const Outlined* outline_of(GlyphSource& glyphs, FontRef font, std::uint32_t glyph, float scale) {
        if (!(scale > 0) || scale > outline_largest) return nullptr;
        const float top = level_top(scale);
        // The last answer for a glyph like it, by a slot of its own: a run's glyphs are few and come again and again.
        Recent& recent = recents[(static_cast<std::size_t>(glyph) * 31 + font.id * 7 + static_cast<std::size_t>(std::bit_cast<std::uint32_t>(top) >> 23)) % recents.size()];
        if (recent.font == font.id && recent.glyph == glyph && recent.top == top) return recent.outline;
        const Outlined* found = nullptr;
        Key k;
        k.u64(7).u64(font.id).u64(glyph).f32(top);
        const std::uint64_t key = k.value();
        if (auto it = outlined.find(key); it != outlined.end()) {
            found = it->second.count > 0 || it->second.blank ? &it->second : nullptr;
        } else {
            // A colour glyph has its layers' image, whatever outline it also has.
            // (Asked of the glyph's mask once: the next level of the same glyph knows.)
            const std::uint64_t which = std::uint64_t{font.id} << 32 | glyph;
            auto known = coloured.find(which);
            if (known == coloured.end()) known = coloured.emplace(which, glyphs.mask(font, glyph, 16, {0, 0}).color != nullptr).first;
            const bool colour = known->second;
            Outlined& kept = outline_kept(key, colour ? xrender::raster::PathData{} : glyphs.outline(font, glyph), {}, top);
            kept.blank = kept.count == 0 && !colour;
            found = kept.count > 0 || kept.blank ? &kept : nullptr;
        }
        recent = {font.id, glyph, top, found};
        return found;
    }

    // The reference's blur of `d` (three box passes a direction, W3C Filter
    // Effects) as the table the shader's blurred_step reads: with D the
    // passes' taps together (the weight of the pixel k along on a pixel),
    // for j from −reach to reach + 1 the sum of D from j on, and D[j − 1].
    // `reach`: the taps' furthest.
    const Outlined& blur_table(int d, int& reach) {
        const int half = d / 2;
        reach = d % 2 == 1 ? 3 * half : 3 * half - 1;
        Key k;
        k.u64(11).u64(static_cast<std::uint64_t>(d));
        const std::uint64_t key = k.value();
        if (auto it = outlined.find(key); it != outlined.end()) return it->second;
        // The taps: each pass the mean of [−lo, +hi] around a pixel.
        const std::array<std::pair<int, int>, 3> passes = d % 2 == 1 ? std::array<std::pair<int, int>, 3>{{{half, half}, {half, half}, {half, half}}}
                                                                    : std::array<std::pair<int, int>, 3>{{{half, half - 1}, {half - 1, half}, {half, half}}};
        const auto span = static_cast<std::size_t>(2 * reach + 1);
        std::vector<double> taps(span, 0.0), next(span, 0.0);
        taps[static_cast<std::size_t>(reach)] = 1;
        for (const auto& [lo, hi] : passes) {
            std::ranges::fill(next, 0.0);
            const double each = 1.0 / (lo + hi + 1);
            for (int from = -reach; from <= reach; ++from) {
                const double weight = taps[static_cast<std::size_t>(from + reach)];
                if (weight == 0) continue;
                for (int step = -lo; step <= hi; ++step) next[static_cast<std::size_t>(from + step + reach)] += weight * each;
            }
            taps.swap(next);
        }
        Outlined made;
        made.first = static_cast<std::uint32_t>(outline_used / 2);
        made.count = static_cast<std::uint32_t>(2 * reach + 2);
        auto tap = [&](int at) { return at < -reach || at > reach ? 0.0 : taps[static_cast<std::size_t>(at + reach)]; };
        for (int j = -reach; j <= reach + 1; ++j) {
            double from_here = 0;
            for (int at = reach; at >= j; --at) from_here += tap(at);
            if (outline_used + 2 > outline_points.size()) outline_points.resize(outline_points.size() + outline_w, 0.0f);
            outline_points[outline_used++] = static_cast<float>(from_here);
            outline_points[outline_used++] = static_cast<float>(tap(j - 1));
        }
        return outlined.emplace(key, made).first->second;
    }

    // A shape's contours for the device at `scale` pixels a unit, by
    // `shape`, a key of what it is: kept, or `make` keeps them (outline_kept
    // under the key and at the level it is given). None when it has no
    // points or the scale is past the largest level.
    template <class Make>
    const Outlined* outline_of(std::uint64_t shape, float scale, Make&& make) {
        if (!(scale > 0) || scale > outline_largest) return nullptr;
        const float top = level_top(scale);
        Key k;
        k.u64(8).u64(shape).f32(top);
        const std::uint64_t key = k.value();
        if (auto it = outlined.find(key); it != outlined.end()) return it->second.count > 0 ? &it->second : nullptr;
        const Outlined& kept = make(key, top);
        return kept.count > 0 ? &kept : nullptr;
    }

    // Twice the rows, what it held copied over (in the list, after the
    // uploads before it); false at the device's limit.
    bool grow_atlas() {
        const std::uint32_t cap = atlas_h * 2;
        if (cap > d.caps().max_texture_size) return false;
        auto t = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, atlas_w, cap, mlibs::rhi::TextureUsage::sampled, "render.atlas"});
        if (!t) return false;
        const std::uint32_t used = std::min(atlas_h, shelf_y + shelf_h);
        if (used > 0) list.copy(atlas, mlibs::rhi::Region{0, 0, atlas_w, used}, *t, 0, 0);
        retired.push_back(atlas);
        atlas = *t;
        atlas_h = cap;
        return true;
    }

    // Everything forgotten: the next frame's masks are made again.
    void empty_atlas() {
        cached.clear();
        shelf_x = shelf_y = shelf_h = 0;
    }

    // A surface's pixels in one upload (its rows lie one after another).
    // Borrowed — not copied into the list — when the surface stays as it is
    // until render() submits: an ImageStore's image does; a glyph source's
    // colour image may be evicted by a later lookup, so it is copied.
    void upload_surface(mlibs::rhi::Texture t, const xrender::raster::Surface& s, bool borrowed) {
        if (s.width() <= 0 || s.height() <= 0) return;
        const mlibs::rhi::Region all{0, 0, static_cast<std::uint32_t>(s.width()), static_cast<std::uint32_t>(s.height())};
        if (borrowed) list.upload_borrowed(t, all, std::as_bytes(s.pixels()));
        else list.upload(t, all, std::as_bytes(s.pixels()));
    }

    // An ImageStore image: uploaded the first time it is drawn.
    mlibs::rhi::Result<mlibs::rhi::Texture> stored_texture(std::uint32_t id, const xrender::raster::Surface& s) {
        const std::uint64_t revision = store ? store->revision(ImageRef{id}) : 0;
        // The same pixels: the same surface at the same revision (an updated
        // image's new surface may reuse the old one's address).
        if (auto it = stored.find(id); it != stored.end() && it->second.surface == &s && it->second.revision == revision) return it->second.texture;
        if (auto it = stored.find(id); it != stored.end()) {
            // Updated in place (ImageStore::update): only what changed since the revision uploaded.
            changed.clear();
            const auto desc = d.describe(it->second.texture);
            if (revision > it->second.revision && desc && desc->width == static_cast<std::uint32_t>(s.width()) &&
                desc->height == static_cast<std::uint32_t>(s.height()) && store->changed_since(ImageRef{id}, it->second.revision, changed)) {
                const auto row_bytes = static_cast<std::uint32_t>(s.width()) * 4;
                const auto bytes = std::as_bytes(s.pixels());
                for (const auto& r : changed) {
                    const auto offset = (static_cast<std::size_t>(r.y) * static_cast<std::size_t>(s.width()) + static_cast<std::size_t>(r.x)) * 4;
                    const auto length = static_cast<std::size_t>(r.height - 1) * row_bytes + static_cast<std::size_t>(r.width) * 4;
                    list.upload_borrowed(it->second.texture,
                                         mlibs::rhi::Region{static_cast<std::uint32_t>(r.x), static_cast<std::uint32_t>(r.y),
                                                            static_cast<std::uint32_t>(r.width), static_cast<std::uint32_t>(r.height)},
                                         bytes.subspan(offset, length), row_bytes);
                }
                it->second.surface = &s;
                it->second.revision = revision;
                return it->second.texture;
            }
            spare(it->second);
            stored.erase(it);
        }
        const auto w = static_cast<std::uint32_t>(s.width()), h = static_cast<std::uint32_t>(s.height());
        mlibs::rhi::Texture t{};
        if (auto it = std::ranges::find_if(spares, [&](const Spare& sp) { return sp.w == w && sp.h == h; }); it != spares.end()) {
            t = it->texture;
            *it = spares.back();
            spares.pop_back();
        } else {
            auto made = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, w, h, mlibs::rhi::TextureUsage::sampled, "render.image"});
            if (!made) return std::unexpected(made.error());
            t = *made;
        }
        upload_surface(t, s, true);
        stored.emplace(id, Stored{t, &s, revision});
        return t;
    }

    // A stored image's texture, kept for another of its size.
    void spare(const Stored& st) {
        if (const auto desc = d.describe(st.texture)) spares.push_back({st.texture, desc->width, desc->height, frame_number});
        else d.destroy(st.texture);
    }

    // The stored images the store no longer holds go (their textures spare).
    void release_stored(const ImageStore* store) {
        std::erase_if(stored, [&](auto& kv) {
            // Still held: the same surface, or the same image updated in place since (uploaded in part when drawn).
            if (const auto* now = store ? store->get(ImageRef{kv.first}) : nullptr;
                now && (now == kv.second.surface || store->revision(ImageRef{kv.first}) > kv.second.revision))
                return false;
            spare(kv.second);
            return true;
        });
    }

    mlibs::rhi::Result<mlibs::rhi::Texture> image_texture(const xrender::raster::Surface& s) {
        auto it = images.find(&s);
        if (it != images.end()) {
            const auto desc = d.describe(it->second);
            if (!desc || desc->width != static_cast<std::uint32_t>(s.width()) || desc->height != static_cast<std::uint32_t>(s.height())) {
                d.destroy(it->second);
                images.erase(it);
                it = images.end();
            }
        }
        if (it == images.end()) {
            auto t = d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::rgba8_unorm, static_cast<std::uint32_t>(s.width()),
                                               static_cast<std::uint32_t>(s.height()), mlibs::rhi::TextureUsage::sampled, "render.image"});
            if (!t) return std::unexpected(t.error());
            it = images.emplace(&s, *t).first;
        }
        // Uploaded again each frame it is drawn in: a surface's pixels may have changed.
        if (std::ranges::find(images_used, &s) == images_used.end()) {
            images_used.push_back(&s);
            upload_surface(it->second, s, false);
        }
        return it->second;
    }

    // A mask by what made it: the atlas's copy, or `make` fills `scratch`
    // and it is placed and uploaded. None: an empty mask, or no room (the
    // frame is encoded again with the atlas emptied).
    template <class Make>
    const Cached* cover(std::uint64_t key, Make&& make) {
        if (auto it = cached.find(key); it != cached.end()) {
            it->second.last = frame_number;
            ++masks_reused;
            return it->second.bounds.empty() && !it->second.color ? nullptr : &it->second;
        }
        scratch.bounds = {};
        scratch.values.clear();
        make(scratch);
        return keep(key, scratch);
    }

    const Cached* keep(std::uint64_t key, const xrender::raster::Coverage& c) {
        ++masks_made;
        Cached e;
        e.bounds = c.bounds;
        e.last = frame_number;
        if (c.empty()) {
            cached.emplace(key, e);
            return nullptr;
        }
        const auto w = static_cast<std::uint32_t>(c.bounds.width), h = static_cast<std::uint32_t>(c.bounds.height);
        if (w > atlas_w) {
            too_wide = c.bounds;
            return nullptr;
        }
        if (shelf_x + w > atlas_w) {
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        while (shelf_y + h > atlas_h)
            if ((atlas_h >= atlas_soft_rows && !atlas_second_pass) || !grow_atlas()) {
                atlas_overflow = true;
                return nullptr;
            }
        e.x = shelf_x;
        e.y = shelf_y;
        shelf_x += w + 1;  // a texel apart: nearest sampling never bleeds, but keep the seam
        shelf_h = std::max(shelf_h, h + 1);
        list.upload(atlas, mlibs::rhi::Region{e.x, e.y, w, h}, std::as_bytes(std::span(c.values)));
        return &cached.emplace(key, e).first->second;
    }

    // ── the effects' pipelines ───────────────────────────────────────────

    mlibs::rhi::Result<mlibs::rhi::GraphicsPipeline> effect_pipeline(EffectRef ref, std::uint64_t generation, const mlibs::rhi::ShaderDesc& fragment) {
        const auto key = std::pair{ref.id, generation};
        if (auto it = effect_pipelines.find(key); it != effect_pipelines.end()) return it->second;
        // An older generation's pipeline goes with the shader it was made of.
        for (auto it = effect_pipelines.begin(); it != effect_pipelines.end();)
            if (it->first.first == ref.id) {
                static_cast<void>(d.wait_idle());
                d.destroy(it->second);
                it = effect_pipelines.erase(it);
            } else {
                ++it;
            }
        auto p = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &shaders::quad::quad_vs, .fragment = &fragment, .blend = mlibs::rhi::Blend::replace(),
                                                    .label = "render.effect"});
        if (!p) return std::unexpected(p.error());
        effect_pipelines.emplace(key, *p);
        return *p;
    }

    mlibs::rhi::Result<void> builtin_pipelines(EffectPass pass) {
        if (pass == EffectPass::blur && !blur_pipeline) {
            auto p = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &shaders::quad::quad_vs, .fragment = &shaders::blur::blur_fs,
                                                        .blend = mlibs::rhi::Blend::replace(), .label = "render.effect.blur"});
            if (!p) return std::unexpected(p.error());
            blur_pipeline = *p;
        }
        if (pass == EffectPass::particles && !particles_pipeline) {
            auto p = d.create(mlibs::rhi::GraphicsPipelineDesc{.vertex = &shaders::particles::particles_vs, .fragment = &shaders::particles::particles_fs,
                                                        .blend = mlibs::rhi::Blend::premultiplied(), .label = "render.effect.particles"});
            if (!p) return std::unexpected(p.error());
            particles_pipeline = *p;
        }
        if (pass == EffectPass::pixelate && !pixelate_pipeline) {
            auto p = d.create(mlibs::rhi::ComputePipelineDesc{&shaders::pixelate::pixelate_cs, "render.effect.pixelate"});
            if (!p) return std::unexpected(p.error());
            pixelate_pipeline = *p;
        }
        return {};
    }

    // The commands that run an effect step (outside any pass).
    void record_effect(const EffectStep& e) {
        const auto w = static_cast<std::int32_t>(e.uniforms.size[0]), h = static_cast<std::int32_t>(e.uniforms.size[1]);
        auto quad = [&](mlibs::rhi::GraphicsPipeline p, mlibs::rhi::Texture from, mlibs::rhi::Texture to, const EffectUniforms& u, mlibs::rhi::Texture aux) {
            list.begin_render({to, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0}});
            list.set_pipeline(p);
            list.push_uniforms(mlibs::rhi::Stage::fragment, 0, u);
            list.bind_texture(mlibs::rhi::Stage::fragment, 0, from, nearest);
            if (aux) list.bind_texture(mlibs::rhi::Stage::fragment, 1, aux, nearest);
            list.draw(3);
            list.end_render();
        };
        if (e.backdrop) {
            // The backdrop: transparent, then what the parent holds under the surface, frosted.
            list.begin_render({e.aux, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0}});
            list.end_render();
            const std::int32_t cx0 = std::max(e.x0, 0), cy0 = std::max(e.y0, 0);
            const std::int32_t cx1 = std::min(e.x0 + w, static_cast<std::int32_t>(e.parent_w)), cy1 = std::min(e.y0 + h, static_cast<std::int32_t>(e.parent_h));
            if (cx1 > cx0 && cy1 > cy0)
                list.copy(e.parent,
                          mlibs::rhi::Region{static_cast<std::uint32_t>(cx0), static_cast<std::uint32_t>(cy0), static_cast<std::uint32_t>(cx1 - cx0),
                                      static_cast<std::uint32_t>(cy1 - cy0)},
                          e.aux, static_cast<std::uint32_t>(cx0 - e.x0), static_cast<std::uint32_t>(cy0 - e.y0));
            if (e.frost > 0) {
                EffectUniforms u = e.uniforms;
                u.derived = {static_cast<float>(e.frost), 0, 0, 0, 0, 0, 0, 0};
                for (int i = 0; i < 3; ++i) {
                    u.derived[1] = 1, u.derived[2] = 0;
                    quad(blur_pipeline, e.aux, e.scratch, u, {});
                    u.derived[1] = 0, u.derived[2] = 1;
                    quad(blur_pipeline, e.scratch, e.aux, u, {});
                }
            }
        }
        switch (e.gpu.pass) {
            case EffectPass::pixel:
                quad(e.pipeline, e.source, e.result, e.uniforms, e.gpu.fragment->samplers >= 2 ? (e.aux ? e.aux : zero_row) : mlibs::rhi::Texture{});
                break;
            case EffectPass::blur: {
                // Three box passes each way, as the reference: source → scratch → result → scratch → … → result.
                EffectUniforms u = e.uniforms;
                mlibs::rhi::Texture from = e.source;
                for (int i = 0; i < 3; ++i) {
                    u.derived[1] = 1, u.derived[2] = 0;
                    quad(blur_pipeline, from, e.scratch, u, {});
                    u.derived[1] = 0, u.derived[2] = 1;
                    quad(blur_pipeline, e.scratch, e.result, u, {});
                    from = e.result;
                }
                break;
            }
            case EffectPass::pixelate: {
                const auto size = static_cast<std::int32_t>(e.uniforms.derived[0]);
                const auto ox = static_cast<std::int32_t>(e.uniforms.derived[1]), oy = static_cast<std::int32_t>(e.uniforms.derived[2]);
                // The reference's first block: the one holding pixel 0 on the grid through the layer's corner.
                auto first = [&](std::int32_t o) {
                    return o + static_cast<std::int32_t>(std::floor(static_cast<float>(-o) / static_cast<float>(size))) * size;
                };
                const std::int32_t fx = first(ox), fy = first(oy);
                const std::int32_t across = (w - fx + size - 1) / size, down = (h - fy + size - 1) / size;
                const PixelateGrid grid{{w, h, size, static_cast<std::int32_t>(e.uniforms.derived[3])}, {fx, fy, across, down}};
                const std::array<mlibs::rhi::Texture, 1> writes{e.result};
                list.begin_compute(writes);
                list.set_pipeline(pixelate_pipeline);
                list.bind_texture(mlibs::rhi::Stage::compute, 0, e.source, nearest);
                list.push_uniforms(mlibs::rhi::Stage::compute, 0, grid);
                list.dispatch(static_cast<std::uint32_t>((across + 7) / 8), static_cast<std::uint32_t>((down + 7) / 8));
                list.end_compute();
                break;
            }
            case EffectPass::particles: {
                const auto size = static_cast<std::int32_t>(e.uniforms.derived[0]);
                const auto x0 = static_cast<std::int32_t>(e.uniforms.derived[1]), y0 = static_cast<std::int32_t>(e.uniforms.derived[2]);
                const auto x1 = static_cast<std::int32_t>(e.uniforms.derived[3]), y1 = static_cast<std::int32_t>(e.uniforms.derived[4]);
                const std::int32_t across = (x1 - x0 + size - 1) / size, down = (y1 - y0 + size - 1) / size;
                list.begin_render({e.result, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0}});
                if (across > 0 && down > 0) {
                    list.set_pipeline(particles_pipeline);
                    list.push_uniforms(mlibs::rhi::Stage::vertex, 0, e.uniforms);
                    list.push_uniforms(mlibs::rhi::Stage::fragment, 0, e.uniforms);
                    list.bind_texture(mlibs::rhi::Stage::fragment, 0, e.source, nearest);
                    list.draw(6, static_cast<std::uint32_t>(across * down));
                }
                list.end_render();
                break;
            }
            case EffectPass::none: break;
        }
    }
};

GpuRenderer::GpuRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuRenderer::GpuRenderer(GpuRenderer&&) noexcept = default;
GpuRenderer& GpuRenderer::operator=(GpuRenderer&&) noexcept = default;
GpuRenderer::~GpuRenderer() = default;

mlibs::rhi::Device& GpuRenderer::device() const noexcept { return impl_->d; }

mlibs::rhi::ShaderFormats gpu_shader_formats() noexcept { return ui::ui_vs.formats(); }

mlibs::rhi::Result<GpuRenderer> GpuRenderer::create(mlibs::rhi::Device& device) {
    auto impl = std::make_unique<Impl>(device);
    if (auto r = impl->init(); !r) return std::unexpected(r.error());
    return GpuRenderer(std::move(impl));
}

mlibs::rhi::Result<RenderReport> GpuRenderer::render(const DisplayList& dl, mlibs::rhi::Texture target, const Resources& res, const GpuOptions& options) {
    Impl& g = *impl_;
    const auto desc = g.d.describe(target);
    if (!desc) return mlibs::rhi::fail(mlibs::rhi::Errc::invalid, "render: a stale target");
    if (desc->format != mlibs::rhi::Format::rgba8_unorm || !mlibs::rhi::has(desc->usage, mlibs::rhi::TextureUsage::color_target))
        return mlibs::rhi::fail(mlibs::rhi::Errc::invalid, "render: the target must be an rgba8_unorm colour target");
    g.width = desc->width;
    g.height = desc->height;

    RenderReport report;
    g.pool_frame();
    g.release_stored(res.images);
    g.store = res.images;
    g.images_used.clear();
    g.list.clear();
    if (auto a = g.ensure_atlas(); !a) return std::unexpected(a.error());
    g.glyph_masks = options.glyph_masks;
    if (g.outline_used > Impl::outline_most) {
        // The outlines kept have outgrown their store: begun again, this frame's kept anew.
        g.outlined.clear();
        g.recents.fill({});
        for (auto& [id, run] : g.kept_runs) run.made = false;  // their instances name points of the store that was
        g.outline_points.assign(Impl::outline_base, 0.0f);
        g.outline_used = g.outline_sent = Impl::outline_base;
        report.outlines_restarted = true;
    }
    const std::size_t outline_before = g.outline_used;
    mlibs::rhi::Error failure{};
    bool failed = false;
    bool bounds_measured=false;
    auto fail = [&](const mlibs::rhi::Error& e) {
        if (!failed) failure = e, failed = true;
    };
    auto check = [&](const auto& r) {
        if (!r) fail(r.error());
        return static_cast<bool>(r);
    };

    // The frame, encoded: the passes and their draws. Run again, once, with
    // the atlas emptied when this frame's masks did not fit what it held.
    auto encode = [&] {
        // The passes' lists of draws are kept for the next frame's: a frame of many thousand draws
        // grows none of them again.
        for (Pass& pass : g.passes)
            if (pass.draws.capacity() > 0) {
                pass.draws.clear();
                g.draw_lists.push_back(std::move(pass.draws));
            }
        g.passes.clear();
        g.graph.clear();
        g.ramp_rows.clear();
        g.lits.clear();
        g.rows.clear();
        g.atlas_overflow = false;
        g.masks_made = g.masks_reused = 0;
        g.asked.clear();
        g.glyph_masks_made = g.path_masks_made = g.glyphs_outlined = 0;
        g.clip_records.clear();
        g.clip_shapes.clear();
        g.kept_draws.clear();
        // The surface drawn into: the target, or an effect layer's own.
        struct Surface {
            std::uint32_t w, h;
        };
        std::vector<Surface> surfaces{{g.width, g.height}};
        auto here = [&]() -> const Surface& { return surfaces.back(); };
        auto whole = [&]() -> xrender::raster::PixelRect { return {0, 0, static_cast<std::int32_t>(here().w), static_cast<std::int32_t>(here().h)}; };

        std::size_t current = 0;
        auto draws_into = [&](mlibs::rhi::Texture t, std::uint32_t w, std::uint32_t h, mlibs::rhi::LoadOp load, std::array<float, 4> clear) {
            Pass p;
            p.target = t;
            p.w = w;
            p.h = h;
            p.load = load;
            p.clear = clear;
            if (!g.draw_lists.empty()) {
                // The roomiest first: the frame's first pass is its largest.
                const auto roomiest = std::ranges::max_element(g.draw_lists, {}, [](const std::vector<Draw>& draws) { return draws.capacity(); });
                p.draws = std::move(*roomiest);
                g.draw_lists.erase(roomiest);
            }
            g.passes.push_back(std::move(p));
            current = g.passes.size() - 1;
        };
        draws_into(target, g.width, g.height, options.clear ? mlibs::rhi::LoadOp::clear : mlibs::rhi::LoadOp::load, premultiplied(options.background));

        std::vector<Affine> transforms{Affine::scale(options.scale, options.scale)};
        std::vector<Clip> clips{{0, 0, static_cast<float>(g.width), static_cast<float>(g.height), {}, nullptr}};
        // An open layer: an ordinary one (drawn into a surface-sized texture), or
        // an effect's (drawn into its own surface, the transform, clip and
        // surface pushed with it).
        struct Open {
            std::size_t  parent;
            mlibs::rhi::Texture texture;
            float        opacity;
            BlendMode    blend;
            bool         effect = false;
            EffectStep   step;
            Rect         device;  // effect: where its surface lies in the parent's
        };
        std::vector<Open> open;

        std::vector<xrender::raster::Stop> stops;
        Path scratch_path;

        auto m = [&]() -> const Affine& { return transforms.back(); };
        auto clip_into = [&](Prim& p, Draw& d, int& flags) {
            const Clip& c = clips.back();
            set(p.clip, c.x0, c.y0, c.x1, c.y1);
            if (c.mask) {
                flags |= flag_clip_mask;
                d.mask = c.mask;
            }
            flags |= static_cast<int>(c.record) << clip_record_shift;
        };
        // A material's normal map and constants (raster's, so both renderers
        // shade with the same numbers); a missing normal map draws unlit, counted.
        auto lit_into = [&](Prim& p, Draw& d, const Material& mat, const Affine& to_device) {
            const xrender::raster::Surface* normal = res.images ? res.images->get(mat.normal) : nullptr;
            if (!normal) {
                ++report.missing_images;
                return;
            }
            auto t = g.stored_texture(mat.normal.id, *normal);
            if (!t) {
                fail(t.error());
                return;
            }
            const xrender::raster::Material rm{.light_x = mat.light.x,
                                      .light_y = mat.light.y,
                                      .light_z = mat.light_z,
                                      .shadow = mat.shadow,
                                      .shadow_strength = mat.shadow_strength,
                                      .bands = mat.bands,
                                      .softness = mat.softness,
                                      .rim = mat.rim,
                                      .rim_strength = mat.rim_strength,
                                      .rim_width = mat.rim_width,
                                      .key = mat.key};
            const xrender::raster::MaterialSetup setup = xrender::raster::material_setup(rm, to_device);
            set(p.pgeo, setup.r00, setup.r01, setup.r10, setup.r11);
            const LitUniforms u = lit_uniforms(setup, static_cast<float>(normal->width()), static_cast<float>(normal->height()));
            if (g.lits.empty() || !(g.lits.back() == u)) g.lits.push_back(u);
            d.lit = static_cast<std::int32_t>(g.lits.size() - 1);
            d.normal = *t;
        };
        // The paint's colour fields; false when it cannot be drawn (a missing image).
        // The shader works in the CPU's bytes: a solid colour premultiplied as
        // raster premultiplies it, an image paint's tint as a byte.
        auto leaf_paint_into = [&](Prim& p, Draw& d, int& flags, const Paint& paint) -> bool {
            p.shape[1] = 255;
            switch (paint.kind) {
                case Paint::Kind::mix: return false; // validated flat leaves only
                case Paint::Kind::solid: {
                    p.color = premultiplied(paint.color);
                    p.paint = {0, 0, 0, 0};
                    return true;
                }
                case Paint::Kind::gradient: {
                    xrender::raster::Paint rp;
                    rp.kind = xrender::raster::Paint::Kind::gradient;
                    rp.gradient = static_cast<xrender::raster::GradientKind>(paint.gradient.kind);
                    rp.start = paint.gradient.start;
                    rp.end = paint.gradient.end;
                    rp.radius = paint.gradient.radius;
                    rp.spread = static_cast<xrender::raster::Spread>(paint.gradient.spread);
                    stops.clear();
                    for (const GradientStop& s : paint.gradient.stops) stops.push_back({s.offset, s.color});
                    rp.stops = stops;
                    const xrender::raster::Shader shader(rp);
                    std::array<xrender::raster::Rgba8, 256> row{};
                    std::ranges::copy(shader.table(), row.begin());
                    g.ramp_rows.push_back(row);
                    // The row under it: where an edge lies after an entry (Shader::hard).
                    for (std::size_t entry = 0; entry < row.size(); ++entry) row[entry] = {shader.hard()[entry], 0, 0, 0};
                    g.ramp_rows.push_back(row);
                    const float kind = paint.gradient.kind == GradientKind::linear ? 1.0f : paint.gradient.kind == GradientKind::radial ? 2.0f : 3.0f;
                    set(p.paint, kind, static_cast<float>(g.ramp_rows.size() / 2 - 1), static_cast<float>(paint.gradient.spread), paint.gradient.radius);
                    if (paint.gradient.kind == GradientKind::radial)
                        set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.radius, 0);
                    else
                        set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.end.x, paint.gradient.end.y);
                    inverse(p, m());
                    return true;
                }
                case Paint::Kind::image:
                case Paint::Kind::image_mask: {
                    const xrender::raster::Surface* img = res.images ? res.images->get(paint.image) : nullptr;
                    if (!img) {
                        ++report.missing_images;
                        return false;
                    }
                    auto t = g.stored_texture(paint.image.id, *img);
                    if (!t) {
                        fail(t.error());
                        return false;
                    }
                    d.image = *t;
                    if (paint.sampling == Sampling::linear) flags |= flag_linear;
                    const bool as_mask = paint.kind == Paint::Kind::image_mask;
                    p.color = as_mask ? premultiplied(paint.color) : std::array<float, 4>{0, 0, 0, byte_of(paint.color.a)};
                    p.paint = {as_mask ? 5.0f : 4.0f, 0, 0, 0};
                    const Affine to_device = m() * paint.image_transform;
                    inverse(p, to_device);
                    p.pinv1[2] = static_cast<float>(img->width());
                    p.pinv1[3] = static_cast<float>(img->height());
                    if (paint.material) lit_into(p, d, *paint.material, to_device);
                    return true;
                }
            }
            return false;
        };
        auto paint_into = [&](Prim& p, Draw& draw, int& flags, const Paint& paint, Rect bounds) -> bool {
            if (paint.kind != Paint::Kind::mix) return leaf_paint_into(p, draw, flags, paint);
            if (!validate_paint_mix(paint)) { ++report.invalid_paints; return false; }
            if (auto ready = g.material_pipelines(); !ready) { fail(ready.error()); return false; }
            const auto parent = g.passes[current].target;
            const auto parent_w = here().w, parent_h = here().h;
            const auto& clip = clips.back();
            // Only the material field needed by this current geometry. This is
            // not a captured old shape, and includes the raster/analytic AA band.
            const float x0 = std::max(0.0f, std::floor(std::max(bounds.x - 2, clip.x0)));
            const float y0 = std::max(0.0f, std::floor(std::max(bounds.y - 2, clip.y0)));
            const float x1 = std::min(static_cast<float>(parent_w), std::ceil(std::min(bounds.right() + 2, clip.x1)));
            const float y1 = std::min(static_cast<float>(parent_h), std::ceil(std::min(bounds.bottom() + 2, clip.y1)));
            if (!(x1 > x0 && y1 > y0)) return false;
            const auto w = static_cast<std::uint32_t>(x1 - x0), h = static_cast<std::uint32_t>(y1 - y0);
            auto field = g.pooled(w, h, mlibs::rhi::Format::rgba16_float,
                mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled, "render.material.field");
            if (!field) { fail(field.error()); return false; }
            draws_into(*field, w, h, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0});
            g.passes[current].material_mix = true;
            const Affine original = m();
            transforms.back() = Affine::translate(-x0, -y0) * original;
            double total = 0; for (const auto& term : paint.mix) total += term.weight;
            for (const auto& term : paint.mix) {
                if (term.weight == 0) continue;
                Draw leaf; int leaf_flags = 0;
                if (!leaf_paint_into(leaf.prim, leaf, leaf_flags, term.paint)) continue;
                transform(leaf.prim, Affine{}, kind_mask, leaf_flags);
                set(leaf.prim.rect, 0, 0, static_cast<float>(w), static_cast<float>(h));
                leaf.prim.shape[3] = static_cast<float>(term.weight / total);
                g.passes[current].draws.push_back(std::move(leaf));
            }
            transforms.back() = original;
            draws_into(parent, parent_w, parent_h, mlibs::rhi::LoadOp::load, {});
            draw.image = *field; draw.lit = -1;
            p.paint = {4, 0, 0, 0}; p.color = {0, 0, 0, byte_of(paint.mix_opacity)};
            p.shape[1] = 255;
            inverse(p, Affine::translate(x0, y0));
            p.pinv1[2] = static_cast<float>(w); p.pinv1[3] = static_cast<float>(h);
            return true;
        };
        auto emit = [&](Draw&& d) { g.passes[current].draws.push_back(std::move(d)); };
        // An analytic rectangle or rounded rectangle.
        auto shape = [&](const Rect& r, const CornerRadii& radii, const Paint& paint) {
            Draw d;
            Prim& p = d.prim;
            const Affine& mm = m();
            const bool rounded = !radii.zero();
            int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
            if (!paint_into(p, d, flags, paint, mm.map(r))) return;
            clip_into(p, d, flags);
            transform(p, mm, rounded ? kind_rrect : kind_rect, flags);
            set(p.rect, r.x, r.y, r.width, r.height);
            if (rounded) {
                const CornerRadii f = radii.fitted(r.size());
                set(p.radii, f.top_left, f.top_right, f.bottom_right, f.bottom_left);
            } else {
                edges(p.radii, mm.map(r));  // the device rectangle, as the CPU maps it
            }
            emit(std::move(d));
        };
        // A mask from the atlas (device space, moved by dx, dy) under a paint.
        auto mask = [&](const Impl::Cached* e, int dx, int dy, const Paint& paint, float scale = 1) {
            if (!e || e->bounds.empty()) return;
            Draw d;
            Prim& p = d.prim;
            int flags = 0;
            if (!paint_into(p, d, flags, paint, {static_cast<float>(e->bounds.x + dx), static_cast<float>(e->bounds.y + dy),
                static_cast<float>(e->bounds.width), static_cast<float>(e->bounds.height)})) return;
            p.shape[1] = byte_of(scale);  // a layered glyph's opacity, applied after, as its layer's pop
            clip_into(p, d, flags);
            transform(p, Affine{}, kind_mask, flags);
            set(p.rect, static_cast<float>(e->bounds.x + dx), static_cast<float>(e->bounds.y + dy), static_cast<float>(e->bounds.width),
                static_cast<float>(e->bounds.height));
            set(p.uv, static_cast<float>(e->x), static_cast<float>(e->y), 0, 0);
            emit(std::move(d));
        };
        // A glyph covered on the device from its outline (KIND_GLYPH), `to` taking its ems to the device.
        // `plain`: a device rectangle, corners of `reach` aside, whose pixels need no outline read — wholly
        // covered (`covered`), or not at all (a ring's inside).
        struct Plain {
            Rect  box;
            float reach = 0;
            bool  covered = true;
        };
        auto outline_glyph = [&](const Impl::Outlined& outline, const Affine& to, const Paint& paint, float scale, const Plain* plain = nullptr) {
            const Rect  box = to.map(outline.bounds);
            const float x0 = std::floor(box.x) - 1, y0 = std::floor(box.y) - 1;
            const float width = std::ceil(box.right()) + 1 - x0, height = std::ceil(box.bottom()) + 1 - y0;
            Draw d;
            Prim& p = d.prim;
            int flags = 0;
            if (!paint_into(p, d, flags, paint, {x0, y0, width, height})) return;
            p.shape[1] = byte_of(scale);
            clip_into(p, d, flags);
            transform(p, Affine{}, kind_glyph, flags);
            set(p.rect, x0, y0, width, height);
            set(p.radii, to.a, to.b, to.c, to.d);
            set(p.uv, to.e, to.f, static_cast<float>(outline.first), static_cast<float>(outline.count));
            if (plain) {
                edges(p.m0, plain->box);
                p.shape[0] = plain->reach;
                p.shape[3] = plain->covered ? 1.0f : 2.0f;
            }
            ++g.glyphs_outlined;
            emit(std::move(d));
        };
        // A glyph whose mask the atlas has not got: true when it was drawn from its outline instead
        // (the frame's masks are limited, and this one is not yet to be made).
        auto outlined_instead = [&](std::uint64_t key, const DrawGlyphs& c, std::uint32_t glyph, const Affine& to, const Paint& paint, float scale) {
            if (g.may_make(key)) return false;
            const Impl::Outlined* outline = g.outline_of(*res.glyphs, c.font, glyph, g.scale_of(to));
            if (!outline) return false;
            if (outline->count > 0) outline_glyph(*outline, to, paint, scale);
            return true;
        };
        // A smoothed rounded rectangle (its stroke of `width`, when not 0) whose mask the atlas has not
        // got, drawn from its outline instead when the frame's masks are limited: true when it was. The
        // outline is the shape's at the origin, by its size, radii and width: rectangles alike share one,
        // and one met before costs no path.
        auto rounded_instead = [&](std::uint64_t key, const Rect& rect, const CornerRadii& corners, float width, const Paint& paint) {
            if (g.cached.contains(key) || g.may_make(key)) return false;
            const Affine& mm = m();
            const Rect    placed{0, 0, rect.width, rect.height};
            const bool    stroke = width > 0;
            const Ring    ring = stroke ? stroke_ring(placed, corners, width) : Ring{};
            Key shape;
            shape.f32(rect.width).f32(rect.height).radii(corners).f32(width);
            const Impl::Outlined* kept = g.outline_of(shape.value(), g.scale_of(mm), [&](std::uint64_t under, float top) -> Impl::Outlined& {
                Path outer, inner;
                outer.add_rounded_rect(stroke ? ring.outer : placed, stroke ? ring.outer_radii : corners);
                if (stroke && ring.hollow) inner.add_rounded_rect(ring.inner, ring.inner_radii);
                return g.outline_kept(under, outer.view(), inner.view(), top);
            });
            if (!kept) return false;
            const Affine to = mm * Affine::translate(rect.x, rect.y);
            Plain plain;
            const bool upright = mm.b == 0 && mm.c == 0 && mm.a > 0 && mm.d > 0 && (!stroke || ring.hollow);
            if (upright) {
                // Past where a smoothed corner begins, (1 + smoothing) radii along the edge, the outline is the rectangle's.
                const Rect&        within = stroke ? ring.inner : placed;
                const CornerRadii& radii = stroke ? ring.inner_radii : corners;
                const float largest = std::max({radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left});
                plain.box = to.map(within);
                plain.reach = (1 + radii.smoothing) * largest * std::max(mm.a, mm.d) + 1;
                plain.covered = !stroke;
            }
            outline_glyph(*kept, to, paint, 1, upright ? &plain : nullptr);
            return true;
        };
        // A box shadow whose mask the atlas has not got, evaluated at the pixel instead (KIND_SHADOW)
        // while it moves or changes (the frame's masks limited): true when it was. Outset shadows
        // under a transform that keeps the box upright (stretched one way or not); the rest are masks.
        auto shadow_instead = [&](std::uint64_t key, const BoxShadow& c) {
            const Affine& mm = m();
            if (c.inset || !(mm.b == 0 && mm.c == 0 && mm.a > 0 && mm.d > 0) || g.cached.contains(key)) return false;
            const bool stretched = mm.a != mm.d;
            // As raster's blur: σ = blur / 2 at the transform's larger scale, d = ⌊σ·3·√(2π)/4 + ½⌋; none below a quarter pixel.
            const float sigma = c.blur / 2 * g.scale_of(mm);
            // (a blur of one pixel's width: the shape's own coverage, the hairline under a knob).
            const int d = sigma > 0.25f ? std::max(1, static_cast<int>(std::floor(sigma * 3.0f * 2.5066282746310002f / 4.0f + 0.5f))) : 1;
            if (d > 192 || (d == 1 && stretched)) return false;
            const Rect shape{c.rect.x + c.offset.x - c.spread, c.rect.y + c.offset.y - c.spread, c.rect.width + 2 * c.spread, c.rect.height + 2 * c.spread};
            if (!(shape.width > 0 && shape.height > 0)) return false;
            if (g.may_make(key, Impl::path_masks_a_frame, g.path_masks_made)) return false;
            int reach = 0;
            const Impl::Outlined& table = g.blur_table(d, reach);
            const CornerRadii round = CornerRadii{std::max(0.0f, c.radii.top_left + c.spread), std::max(0.0f, c.radii.top_right + c.spread),
                                                  std::max(0.0f, c.radii.bottom_right + c.spread), std::max(0.0f, c.radii.bottom_left + c.spread)}
                                          .fitted({shape.width, shape.height});
            const Rect  placed = mm.map(shape);
            const float x0 = std::floor(placed.x) - static_cast<float>(reach) - 1, y0 = std::floor(placed.y) - static_cast<float>(reach) - 1;
            const float width = std::ceil(placed.right()) + static_cast<float>(reach) + 1 - x0, height = std::ceil(placed.bottom()) + static_cast<float>(reach) + 1 - y0;
            // The box the shadow shows outside of: its circles' exact area, or — its corners smoothed, or
            // stretched to ellipses — its outline's.
            const Impl::Outlined* box_outline = nullptr;
            if ((c.radii.smoothing > 0 || stretched) && !c.radii.zero()) {
                Key outline;
                outline.f32(c.rect.width).f32(c.rect.height).radii(c.radii).f32(0);
                box_outline = g.outline_of(outline.value(), g.scale_of(mm), [&](std::uint64_t under, float top) -> Impl::Outlined& {
                    Path path;
                    path.add_rounded_rect({0, 0, c.rect.width, c.rect.height}, c.radii);
                    return g.outline_kept(under, path.view(), {}, top);
                });
                if (!box_outline) return false;
            }
            Draw draw;
            Prim& p = draw.prim;
            int flags = 0;
            if (!paint_into(p, draw, flags, Paint::solid(c.color), {x0, y0, width, height})) return true;
            p.shape[0] = mm.a / mm.d;  // a corner's reach across, by its reach down: an ellipse's when stretched
            p.shape[1] = 255;
            clip_into(p, draw, flags);
            transform(p, Affine{}, kind_shadow, flags);
            p.m1[0] = static_cast<float>(table.first);
            p.m1[1] = static_cast<float>(reach);
            set(p.rect, x0, y0, width, height);
            set(p.uv, placed.x, placed.y, placed.width, placed.height);
            set(p.radii, round.top_left * mm.d, round.top_right * mm.d, round.bottom_right * mm.d, round.bottom_left * mm.d);  // down the column
            const CornerRadii box = c.radii.fitted({c.rect.width, c.rect.height});
            edges(p.m0, mm.map(c.rect));
            set(p.pgeo, box.top_left * mm.a, box.top_right * mm.a, box.bottom_right * mm.a, box.bottom_left * mm.a);
            if (box_outline) {
                const Affine to = mm * Affine::translate(c.rect.x, c.rect.y);
                set(p.pinv0, to.a, to.d, to.e, to.f);
                p.pinv1[0] = static_cast<float>(box_outline->first);
                p.pinv1[1] = static_cast<float>(box_outline->count);
            }
            ++g.glyphs_outlined;
            emit(std::move(draw));
            return true;
        };
        // A path filled by the non-zero rule whose mask the atlas has not got, drawn from its outline
        // instead while it moves or changes (the frame's masks limited): true when it was.
        auto path_instead = [&](std::uint64_t key, const PathView& path, const Paint& paint) {
            if (g.cached.contains(key) || g.may_make(key, Impl::path_masks_a_frame, g.path_masks_made)) return false;
            const Affine& mm = m();
            Key shape;
            shape.path(path);
            const Impl::Outlined* kept = g.outline_of(shape.value(), g.scale_of(mm),
                                                      [&](std::uint64_t under, float top) -> Impl::Outlined& { return g.outline_kept(under, path, {}, top); });
            if (!kept) return false;
            const Rect box = mm.map(kept->bounds);
            if (static_cast<float>(kept->count) * (box.width + 2) * (box.height + 2) > Impl::outline_reads_most) return false;
            outline_glyph(*kept, mm, paint, 1);
            return true;
        };
        // What every device-space mask's key starts with: the transform and the clip it was rasterised within.
        auto device_key = [&](std::uint64_t kind) {
            Key k;
            k.u64(kind).affine(m()).pixels(pixels(clips.back()));
            return k;
        };
        // A texture's pixels (w × h) over `dest` (local space under `mm`).
        // As raster's draw_image: the destination filled with an image paint
        // whose transform takes the source rectangle onto it.
        auto texture_image = [&](mlibs::rhi::Texture t, std::uint32_t w, std::uint32_t h, const Rect& source, const Rect& dest, Sampling sampling,
                                 float opacity, const Affine& mm) {
            if (source.empty() || dest.empty()) return;
            Draw d;
            Prim& p = d.prim;
            d.image = t;
            int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
            if (sampling == Sampling::linear) flags |= flag_linear;
            clip_into(p, d, flags);
            transform(p, mm, kind_image, flags);
            set(p.rect, dest.x, dest.y, dest.width, dest.height);
            edges(p.radii, mm.map(dest));
            p.shape[1] = 255;
            p.paint = {4, 0, 0, 0};
            p.color = {0, 0, 0, byte_of(opacity)};  // the white tint's byte times the opacity's
            inverse(p, mm * Affine::translate(dest.x, dest.y) * Affine::scale(dest.width / source.width, dest.height / source.height) *
                           Affine::translate(-source.x, -source.y));
            p.pinv1[2] = static_cast<float>(w);
            p.pinv1[3] = static_cast<float>(h);
            emit(std::move(d));
        };
        // A layer's pixels over its parent's, at its opacity under the clip, as
        // raster's pop_layer composites them.
        auto layer_image = [&](mlibs::rhi::Texture t, std::uint32_t w, std::uint32_t h, float opacity) {
            Draw d;
            Prim& p = d.prim;
            d.image = t;
            int flags = flag_axis | flag_layer;
            clip_into(p, d, flags);
            transform(p, Affine{}, kind_image, flags);
            set(p.rect, 0, 0, static_cast<float>(w), static_cast<float>(h));
            edges(p.radii, {0, 0, static_cast<float>(w), static_cast<float>(h)});
            p.shape = {0, 255, byte_of(opacity), 0};
            p.pinv1[2] = static_cast<float>(w);
            p.pinv1[3] = static_cast<float>(h);
            emit(std::move(d));
        };
        auto image = [&](const xrender::raster::Surface& img, const Rect& source, const Rect& dest, Sampling sampling, float opacity, const Affine& mm) {
            auto t = g.image_texture(img);
            if (!t) {
                fail(t.error());
                return;
            }
            texture_image(*t, static_cast<std::uint32_t>(img.width()), static_cast<std::uint32_t>(img.height()), source, dest, sampling, opacity, mm);
        };
        // A rounded clip covered on the device from its outline while the frame's masks are limited (D-188):
        // no mask of the surface's size is rasterised or sent, whatever the transform — a record of the
        // frame's instead, read by every primitive drawn under it. True when pushed; false (a mask then)
        // with the masks unlimited, past the records' room or their depth, or turned with too large an outline.
        auto rounded_clip = [&](const Rect& rect, const CornerRadii& corners) {
            const Clip& below = clips.back();
            if (g.glyph_masks == std::numeric_limits<std::size_t>::max() || corners.zero() || !(rect.width > 0 && rect.height > 0)) return false;
            if (below.depth >= Impl::clip_depth_most || g.clip_records.size() + Impl::clip_floats > Impl::outline_base) return false;
            const Affine& mm = m();
            const Rect    placed{0, 0, rect.width, rect.height};
            Key shape;
            shape.f32(rect.width).f32(rect.height).radii(corners).f32(0);  // as a fill of it: the one outline
            const Impl::Outlined* kept = g.outline_of(shape.value(), g.scale_of(mm), [&](std::uint64_t under, float top) -> Impl::Outlined& {
                Path outer;
                outer.add_rounded_rect(placed, corners);
                return g.outline_kept(under, outer.view(), {}, top);
            });
            if (!kept) return false;
            const Affine to = mm * Affine::translate(rect.x, rect.y);
            const Rect   bounds = to.map(placed);
            const bool   upright = mm.b == 0 && mm.c == 0 && mm.a > 0 && mm.d > 0;
            if (!upright && static_cast<float>(kept->count) * (bounds.width + 2) * (bounds.height + 2) > Impl::outline_reads_most) return false;
            const float largest = std::max({corners.top_left, corners.top_right, corners.bottom_right, corners.bottom_left});
            const float reach = upright ? (1 + corners.smoothing) * largest * std::max(mm.a, mm.d) + 1 : -1.0f;
            const auto  record = static_cast<std::uint32_t>(g.clip_records.size() / Impl::clip_floats) + 1;
            for (const float value : {to.a, to.b, to.c, to.d, to.e, to.f, static_cast<float>(kept->first), static_cast<float>(kept->count), bounds.x, bounds.y,
                                      bounds.right(), bounds.bottom(), reach, static_cast<float>(below.record)})
                g.clip_records.push_back(value);
            g.clip_shapes.push_back({mm, rect, corners, below.shape});
            // Its pixels' bounds, whole pixels (the edge's own coverage is the outline's).
            clips.push_back({std::max(below.x0, std::floor(bounds.x)), std::max(below.y0, std::floor(bounds.y)), std::min(below.x1, std::ceil(bounds.right())),
                             std::min(below.y1, std::ceil(bounds.bottom())), below.mask, below.coverage, below.key, record, below.depth + 1,
                             static_cast<int>(g.clip_shapes.size()) - 1});
            return true;
        };
        // A clip's mask as a texture of the surface's size, its rounded clips covered from their outlines
        // rasterised into it: what a pass that reads no records (a blended layer's) clips by. None: no mask
        // and no such clip.
        auto mask_of = [&](const Clip& c) -> mlibs::rhi::Texture {
            if (c.shape < 0) return c.mask;
            const std::uint32_t W = here().w, H = here().h;
            Key key;
            key.u64(12).u64(W).u64(H).u64(c.key);
            for (int at = c.shape; at >= 0; at = g.clip_shapes[static_cast<std::size_t>(at)].below) {
                const ClipShape& shape = g.clip_shapes[static_cast<std::size_t>(at)];
                key.affine(shape.to).rect(shape.rect).radii(shape.radii);
            }
            const std::uint64_t k = key.value();
            if (auto it = g.clip_masks.find(k); it != g.clip_masks.end()) {
                it->second.last = g.frame_number;
                return it->second.texture;
            }
            auto values = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(W) * H, 255);
            if (c.coverage) *values = *c.coverage;
            for (int at = c.shape; at >= 0; at = g.clip_shapes[static_cast<std::size_t>(at)].below) {
                const ClipShape& shape = g.clip_shapes[static_cast<std::size_t>(at)];
                xrender::raster::Coverage& cov = g.scratch;
                cov.bounds = {};
                cov.values.clear();
                xrender::raster::PathBuilder pb;
                xrender::raster::rounded_rect_path(pb, shape.rect, shape.radii);
                xrender::raster::fill_coverage(pb.data(), shape.to, xrender::raster::FillRule::nonzero, whole(), cov);
                for (std::int32_t y = 0; y < static_cast<std::int32_t>(H); ++y)
                    for (std::int32_t x = 0; x < static_cast<std::int32_t>(W); ++x) {
                        std::uint8_t& value = (*values)[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
                        const bool    within = x >= cov.bounds.x && y >= cov.bounds.y && x < cov.bounds.x + cov.bounds.width && y < cov.bounds.y + cov.bounds.height;
                        value = within ? static_cast<std::uint8_t>(xrender::raster::mul255(cov.at(x, y), value)) : std::uint8_t{0};
                    }
            }
            auto t = g.d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, W, H, mlibs::rhi::TextureUsage::sampled, "render.clip_mask"});
            if (!t) {
                fail(t.error());
                return c.mask;
            }
            g.list.upload(*t, mlibs::rhi::Region{0, 0, W, H}, std::as_bytes(std::span(*values)));
            g.clip_masks.emplace(k, Impl::ClipMask{*t, values, g.frame_number});
            return *t;
        };
        // A clip by a mask, surface-sized, under the clip below it: made
        // (`make` rasterises it) and uploaded only when nothing made the same.
        auto push_mask_clip = [&](Key key, auto&& make) {
            const Clip& below = clips.back();
            const std::uint32_t W = here().w, H = here().h;
            const std::uint64_t k = key.u64(W).u64(H).u64(below.key).value();
            if (auto it = g.clip_masks.find(k); it != g.clip_masks.end()) {
                it->second.last = g.frame_number;
                clips.push_back({below.x0, below.y0, below.x1, below.y1, it->second.texture, it->second.coverage, k, below.record, below.depth, below.shape});
                return;
            }
            xrender::raster::Coverage& c = g.scratch;
            c.bounds = {};
            c.values.clear();
            make(c);
            auto values = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(W) * H, 0);
            for (std::int32_t y = std::max(0, c.bounds.y); y < std::min<std::int32_t>(static_cast<std::int32_t>(H), c.bounds.y + c.bounds.height); ++y)
                for (std::int32_t x = std::max(0, c.bounds.x); x < std::min<std::int32_t>(static_cast<std::int32_t>(W), c.bounds.x + c.bounds.width); ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x);
                    const std::uint32_t under = below.coverage ? (*below.coverage)[i] : 255u;
                    (*values)[i] = static_cast<std::uint8_t>(xrender::raster::mul255(c.at(x, y), under));
                }
            auto t = g.d.create(mlibs::rhi::TextureDesc{mlibs::rhi::Format::r8_unorm, W, H, mlibs::rhi::TextureUsage::sampled, "render.clip_mask"});
            if (!t) {
                fail(t.error());
                clips.push_back(below);
                return;
            }
            g.list.upload(*t, mlibs::rhi::Region{0, 0, W, H}, std::as_bytes(std::span(*values)));
            g.clip_masks.emplace(k, Impl::ClipMask{*t, values, g.frame_number});
            clips.push_back({below.x0, below.y0, below.x1, below.y1, *t, std::move(values), k, below.record, below.depth, below.shape});
        };
        // An ordinary layer opens: drawn into a surface-sized texture.
        auto open_layer = [&](float opacity, BlendMode mode) {
            auto t = g.pooled(here().w, here().h, mlibs::rhi::Format::rgba8_unorm, mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled, "render.layer");
            if (!t) {
                fail(t.error());
                return;
            }
            open.push_back({current, *t, opacity, mode});
            draws_into(*t, here().w, here().h, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0});
        };
        // An ordinary layer closes: over its parent, within the clip and under its
        // mask as the CPU composites it (what it holds was clipped as it was
        // drawn, and is masked again), or blended with what lies under it.
        auto close_layer = [&](const Open& o) {
            const mlibs::rhi::Texture parent = g.passes[o.parent].target;
            const std::uint32_t W = g.passes[o.parent].w, H = g.passes[o.parent].h;
            const Clip& c = clips.back();
            if (o.blend != BlendMode::normal) {
                xrender::raster::PixelRect b = pixels(c);
                const std::int32_t x1 = std::min<std::int32_t>(b.x + b.width, static_cast<std::int32_t>(W));
                const std::int32_t y1 = std::min<std::int32_t>(b.y + b.height, static_cast<std::int32_t>(H));
                b.x = std::max(b.x, 0);
                b.y = std::max(b.y, 0);
                b.width = x1 - b.x;
                b.height = y1 - b.y;
                if (!b.empty()) {
                    auto backdrop = g.pooled(W, H, mlibs::rhi::Format::rgba8_unorm, mlibs::rhi::TextureUsage::sampled, "render.backdrop");
                    if (!backdrop) {
                        fail(backdrop.error());
                        return;
                    }
                    Pass p;
                    p.kind = Pass::Kind::blend;
                    p.target = parent;
                    p.w = W;
                    p.h = H;
                    p.blend = {o.texture,
                               *backdrop,
                               {static_cast<std::uint32_t>(b.x), static_cast<std::uint32_t>(b.y), static_cast<std::uint32_t>(b.width),
                                static_cast<std::uint32_t>(b.height)},
                               o.blend,
                               static_cast<std::uint32_t>(std::clamp(o.opacity, 0.0f, 1.0f) * 255.0f + 0.5f),
                               mask_of(c),
                               {c.x0, c.y0, c.x1, c.y1}};
                    g.passes.push_back(std::move(p));
                }
                draws_into(parent, W, H, mlibs::rhi::LoadOp::load, {});
                return;
            }
            draws_into(parent, W, H, mlibs::rhi::LoadOp::load, {});
            layer_image(o.texture, W, H, o.opacity);
        };
        // An effect layer (D-68) opens: its content offscreen in a surface of its
        // own, larger than the layer by the effect's reach, as the CPU renderer
        // prepares it. False: drawn as an ordinary layer (no GPU path, or no pop).
        auto open_effect = [&](const PushLayer& push, std::size_t index, std::size_t& skip_to) -> bool {
            const auto& all = dl.commands();
            const EffectInfo* info = effects().info(push.effect);
            std::size_t pop = index + 1;
            for (int depth = 1; pop < all.size(); ++pop) {
                if (std::holds_alternative<PushLayer>(all[pop])) ++depth;
                else if (std::holds_alternative<PopLayer>(all[pop]) && --depth == 0) break;
            }
            if (pop >= all.size()) return false;  // unbalanced: an ordinary layer
            std::vector<float> params(push.params.begin(), push.params.end());
            if (info) for (std::size_t k = params.size(); k < info->params.size(); ++k) params.push_back(info->params[k].value);
            if (info && info->identity && info->identity(params)) return false;
            const bool drawable = info && info->gpu.pass != EffectPass::none &&
                                  (info->gpu.pass != EffectPass::pixel || (info->gpu.fragment && template_shaped(*info->gpu.fragment)));
            if (!drawable) {
                ++report.effects_skipped;
                return false;  // drawn without it
            }
            EffectStep step;
            step.gpu = info->gpu;
            const float scale = transforms.front().max_scale();
            const float reach = std::ceil(effects().reach(push.effect, params) * scale) + 1;
            const float viewport_reach = info->input_radius ? std::max(reach, std::ceil(info->input_radius(params, scale))) : reach;
            Rect layer=m().map(push.bounds);
            if(push.bounds_mode==LayerBoundsMode::content) {
                if(!bounds_measured){(void)g.layer_bounds.measure(dl,Affine::scale(options.scale,options.scale),options.scale,res);bounds_measured=true;}
                const auto* measured=g.layer_bounds.at(index);
                if(!measured){++report.effects_skipped;return false;}
                Point origin;
                for(const auto& parent:open)if(parent.effect){origin.x+=parent.device.x;origin.y+=parent.device.y;}
                layer=measured->area.offset({-origin.x,-origin.y});
            }
            const Rect screen{-viewport_reach, -viewport_reach, static_cast<float>(here().w) + 2 * viewport_reach, static_cast<float>(here().h) + 2 * viewport_reach};
            const Rect wanted = Rect{layer.x - reach, layer.y - reach, layer.width + 2 * reach, layer.height + 2 * reach}.intersection(screen);
            if (wanted.empty()) {  // nothing of it on the surface
                report.commands += pop - index;
                skip_to = pop;
                return true;
            }
            const float x0 = std::floor(wanted.x), y0 = std::floor(wanted.y);
            const auto w = static_cast<std::uint32_t>(std::ceil(wanted.right()) - x0), h = static_cast<std::uint32_t>(std::ceil(wanted.bottom()) - y0);
            const Rect inside = layer.offset({-x0, -y0});
            EffectFrame frame;
            if (info->gpu.derive) info->gpu.derive(params, scale, inside, frame);
            step.uniforms.size = {static_cast<float>(w), static_cast<float>(h), scale, 0};
            step.uniforms.layer = {inside.x, inside.y, inside.width, inside.height};
            for (std::size_t k = 0; k < std::min<std::size_t>(params.size(), 16); ++k) step.uniforms.params[k] = params[k];
            step.uniforms.derived = frame.derived;
            // The shapes in the surface's pixels, as the CPU renderer maps them.
            {
                const Affine& mm = m();
                const float k = mm.max_scale();
                std::size_t n = 0;
                auto put = [&](const Rect& r, const CornerRadii& c) {
                    if (n >= max_shapes) return;
                    std::copy_n(std::array<float, 8>{r.x, r.y, r.width, r.height, c.top_left * k, c.top_right * k, c.bottom_right * k, c.bottom_left * k}.begin(),
                                8, step.uniforms.shapes.begin() + static_cast<std::ptrdiff_t>(n * 8));
                    ++n;
                };
                for (const EffectShape& sh : push.shapes) put(mm.map(sh.rect).offset({-x0, -y0}), sh.radii);
                if (n == 0) {
                    const Rect r = inside;
                    std::copy_n(std::array<float, 8>{r.x, r.y, r.width, r.height, 0, 0, 0, 0}.begin(), 8, step.uniforms.shapes.begin());
                    n = 1;
                }
                step.uniforms.shape_info = {static_cast<float>(n), 0, 0, 0};
            }
            const auto rgba = mlibs::rhi::TextureUsage::color_target | mlibs::rhi::TextureUsage::sampled;
            if (!check(g.builtin_pipelines(info->gpu.pass))) return true;
            if (info->backdrop) {
                // Frosting runs the blur passes.
                if (!check(g.builtin_pipelines(EffectPass::blur))) return true;
                step.backdrop = true;
                step.parent = g.passes[current].target;
                step.parent_w = here().w;
                step.parent_h = here().h;
                step.x0 = static_cast<std::int32_t>(x0);
                step.y0 = static_cast<std::int32_t>(y0);
                step.frost = info->backdrop_blur ? box_radius(info->backdrop_blur(params), scale) : 0;
                auto aux = g.pooled(w, h, mlibs::rhi::Format::rgba8_unorm, rgba, "render.effect.backdrop");
                if (!check(aux)) return true;
                step.aux = *aux;
                if (step.frost > 0) {
                    auto scratch = g.pooled(w, h, mlibs::rhi::Format::rgba8_unorm, rgba, "render.effect.frost");
                    if (!check(scratch)) return true;
                    step.scratch = *scratch;
                }
            }
            if (info->gpu.pass == EffectPass::pixel) {
                auto p = g.effect_pipeline(push.effect, effects().generation(push.effect), *info->gpu.fragment);
                if (!check(p)) return true;
                step.pipeline = *p;
                if (info->gpu.rows) {
                    std::vector<float>& values = g.rows.emplace_back(h);
                    info->gpu.rows(params, scale, values);
                    auto aux = g.pooled(h, 1, mlibs::rhi::Format::r32_float, mlibs::rhi::TextureUsage::sampled, "render.effect.rows");
                    if (!check(aux)) return true;
                    step.aux = *aux;
                    g.list.upload(step.aux, mlibs::rhi::Region{0, 0, h, 1}, std::as_bytes(std::span(values)));
                }
            }
            auto source = g.pooled(w, h, mlibs::rhi::Format::rgba8_unorm, rgba, "render.effect.source");
            auto result = g.pooled(w, h, mlibs::rhi::Format::rgba8_unorm,
                                   info->gpu.pass == EffectPass::pixelate ? mlibs::rhi::TextureUsage::compute_write | mlibs::rhi::TextureUsage::sampled : rgba,
                                   "render.effect.result");
            if (!check(source) || !check(result)) return true;
            step.source = *source;
            step.result = *result;
            if (info->gpu.pass == EffectPass::blur) {
                auto scratch = g.pooled(w, h, mlibs::rhi::Format::rgba8_unorm, rgba, "render.effect.scratch");
                if (!check(scratch)) return true;
                step.scratch = *scratch;
            }
            open.push_back({current, step.source, push.opacity, push.blend, true, step, {x0, y0, static_cast<float>(w), static_cast<float>(h)}});
            draws_into(step.source, w, h, mlibs::rhi::LoadOp::clear, {0, 0, 0, 0});
            transforms.push_back(Affine::translate(-x0, -y0) * m());
            clips.push_back({0, 0, static_cast<float>(w), static_cast<float>(h), {}, nullptr});
            surfaces.push_back({w, h});
            return true;
        };
        // An effect layer closes: the effect runs on its surface, and the result
        // goes where the layer is, under the clip it was pushed in (and its blend
        // mode, as a layer of its own), as the CPU renderer composites it.
        auto close_effect = [&](const Open& o) {
            transforms.pop_back();
            clips.pop_back();
            surfaces.pop_back();
            Pass p;
            p.kind = Pass::Kind::effect;
            p.effect = o.step;
            g.passes.push_back(std::move(p));
            const mlibs::rhi::Texture parent = g.passes[o.parent].target;
            const std::uint32_t W = g.passes[o.parent].w, H = g.passes[o.parent].h;
            draws_into(parent, W, H, mlibs::rhi::LoadOp::load, {});
            const bool blended = o.blend != BlendMode::normal;
            if (blended) open_layer(1, o.blend);
            const auto w = static_cast<std::uint32_t>(o.device.width), h = static_cast<std::uint32_t>(o.device.height);
            texture_image(o.step.result, w, h, {0, 0, o.device.width, o.device.height}, o.device, Sampling::nearest, o.opacity, Affine{});
            if (blended && !open.empty()) {
                const Open inner = open.back();
                open.pop_back();
                close_layer(inner);
            }
        };

        // ── kept runs: their glyphs on the device ──────────────────────────
        // A run begins: what the renderer has of it serves (the same revision, the scale within what its
        // outlines' levels were chosen for), or it is made as its commands are met. Only where no glyph
        // mask is ever made (a host's frames); a readback's glyphs are their masks.
        const std::span<const DisplayList::KeptRun> runs = g.glyph_masks == 0 && g.kept_pipeline ? dl.kept_runs() : std::span<const DisplayList::KeptRun>{};
        std::size_t                 next_run = 0;
        const DisplayList::KeptRun* in_run = nullptr;
        Impl::KeptRun*              keeping = nullptr;
        bool                        making = false;
        std::size_t                 part_at = 0;
        auto run_begins = [&](const DisplayList::KeptRun& run) {
            in_run = &run;
            const float now = g.scale_of(m());
            if (!(now > 0)) return;
            keeping = &g.kept_runs[run.id];
            keeping->last = g.frame_number;
            bool serves = keeping->made && keeping->revision == run.revision;
            if (serves) {
                const float by = now / keeping->scale;
                serves = by > keeping->least && by <= keeping->most;
            }
            making = !serves;
            part_at = 0;
            if (making) {
                keeping->revision = run.revision;
                keeping->parts.clear();
                keeping->scale = now;
                keeping->least = 0;
                keeping->most = std::numeric_limits<float>::max();
                keeping->made = false;
                g.kept_staging.clear();
            }
        };
        auto run_ends = [&]() {
            if (keeping && making) {
                const auto bytes = static_cast<std::uint32_t>(g.kept_staging.size() * sizeof(KeptGlyph));
                if (bytes > keeping->capacity) {
                    if (keeping->buffer) g.d.destroy(keeping->buffer);
                    keeping->buffer = {};
                    std::uint32_t cap = std::max<std::uint32_t>(64 * sizeof(KeptGlyph), keeping->capacity);
                    while (cap < bytes) cap *= 2;
                    auto made = g.d.create(mlibs::rhi::BufferDesc{cap, mlibs::rhi::BufferUsage::vertex, "render.kept"});
                    if (!made) {
                        fail(made.error());
                    } else {
                        keeping->buffer = *made;
                        keeping->capacity = cap;
                    }
                }
                if (bytes > 0 && keeping->buffer) g.list.upload(keeping->buffer, 0, std::span<const KeptGlyph>(g.kept_staging));
                keeping->made = bytes == 0 || keeping->buffer;
                report.glyphs_kept_made += g.kept_staging.size();
            }
            in_run = nullptr;
            keeping = nullptr;
            making = false;
        };
        // The glyphs of a part of the run, drawn from the run's instances under the transform and the clip here.
        auto draw_kept = [&](const Impl::KeptPart& part) {
            if (part.count == 0) return;
            const Affine& mm = m();
            const Clip&   clip = clips.back();
            const int     flags = (clip.mask ? flag_clip_mask : 0) | static_cast<int>(clip.record) << clip_record_shift;
            const KeptUniforms uniforms{{mm.a, mm.b, mm.c, mm.d}, {mm.e, mm.f, static_cast<float>(flags), 0}, {clip.x0, clip.y0, clip.x1, clip.y1}};
            g.glyphs_outlined += part.count;
            if (!making) report.glyphs_kept += part.count;
            auto& draws = g.passes[current].draws;
            if (!draws.empty() && draws.back().kept >= 0 && draws.back().mask == clip.mask) {
                // The glyphs after the last ones drawn, in the same place under the same clip: one draw of both.
                Impl::KeptDraw& last = g.kept_draws[static_cast<std::size_t>(draws.back().kept)];
                if (last.run == keeping && last.first + last.count == part.first && last.uniforms == uniforms) {
                    last.count += part.count;
                    return;
                }
            }
            Draw d;
            d.kept = static_cast<std::int32_t>(g.kept_draws.size());
            d.mask = clip.mask;
            g.kept_draws.push_back({keeping, part.first, part.count, uniforms});
            emit(std::move(d));
        };
        // A DrawGlyphs of a kept run: true when its glyphs were drawn from the run's instances (made here
        // when the run is being made). False: encoded as any — a paint that is not one colour, a colour
        // glyph, a glyph past the largest level, inside a layer.
        auto kept_glyphs = [&](const DrawGlyphs& c, std::size_t command) -> bool {
            const bool here = open.empty() && !g.passes[current].material_mix;
            if (!making) {
                if (part_at >= keeping->parts.size() || keeping->parts[part_at].command != command) return false;
                const Impl::KeptPart& part = keeping->parts[part_at++];
                if (!part.kept || !here) return false;
                draw_kept(part);
                return true;
            }
            const Affine& mm = m();
            Impl::KeptPart part{command, static_cast<std::uint32_t>(g.kept_staging.size()), 0, false};
            const auto     back = mm.inverse();
            bool           fits = here && back.has_value() && c.paint.kind == Paint::Kind::solid;
            float          least = keeping->least, most = keeping->most;
            for (std::size_t i = 0; fits && i < c.glyphs.size(); ++i) {
                const Placed pl = place(c, i, mm);
                Color        colour = c.paint.color;
                if (pl.style) {
                    const float opacity = std::clamp(pl.style->opacity, 0.0f, 1.0f);
                    if (opacity <= 0) continue;
                    const Color tint = pl.style->tint;
                    const float by = std::clamp(tint.a, 0.0f, 1.0f);
                    colour = {colour.r + (tint.r - colour.r) * by, colour.g + (tint.g - colour.g) * by, colour.b + (tint.b - colour.b) * by, colour.a * opacity};
                }
                // An outline a level finer than this scale asks: it serves from half this scale to twice it,
                // never coarser than the reference's own flattening.
                const float           scale = g.scale_of(pl.outline);
                const Impl::Outlined* outline = g.outline_of(*res.glyphs, c.font, c.glyphs[i].id, scale * 2);
                if (!outline) {
                    fits = false;
                    break;
                }
                if (outline->count == 0) continue;
                const float top = Impl::level_top(scale * 2);
                most = std::min(most, top / scale);
                least = std::max(least, top / (4 * scale));
                const Affine local = *back * pl.outline;
                g.kept_staging.push_back({{local.a, local.b, local.c, local.d},
                                          {local.e, local.f, static_cast<float>(outline->first), static_cast<float>(outline->count)},
                                          {outline->bounds.x, outline->bounds.y, outline->bounds.width, outline->bounds.height},
                                          premultiplied(colour)});
            }
            if (fits) {
                part.kept = true;
                part.count = static_cast<std::uint32_t>(g.kept_staging.size()) - part.first;
                keeping->least = least;
                keeping->most = most;
            } else {
                g.kept_staging.resize(part.first);
            }
            keeping->parts.push_back(part);
            if (!part.kept) return false;
            draw_kept(part);
            return true;
        };

        const auto& all = dl.commands();
        for (std::size_t index = 0; index < all.size() && !failed; ++index) {
            if (in_run && index >= in_run->first + in_run->count) run_ends();
            while (!in_run && next_run < runs.size() && runs[next_run].first < index) ++next_run;  // (begun within what was skipped)
            if (!in_run && next_run < runs.size() && index == runs[next_run].first) run_begins(runs[next_run++]);
            ++report.commands;
            if (const auto* push = std::get_if<PushLayer>(&all[index]); push && push->effect.id != 0) {
                std::size_t skip_to = index;
                if (!open_effect(*push, index, skip_to)) open_layer(push->opacity, push->blend);
                index = skip_to;
                continue;
            }
            std::visit(
                [&](const auto& c) {
                    using T = std::decay_t<decltype(c)>;
                    if constexpr (std::same_as<T, FillRect>) {
                        shape(c.rect, {}, c.paint);
                    } else if constexpr (std::same_as<T, FillRoundRect>) {
                        if (c.radii.smoothing > 0 && !c.radii.zero()) {
                            // Smoothed corners have no closed-form area: the shared outline's mask, as a path is drawn.
                            // By what the outline is made of: one the atlas has costs no path.
                            const auto key = device_key(9).rect(c.rect).radii(c.radii).value();
                            if (rounded_instead(key, c.rect, c.radii, 0, c.paint)) return;
                            mask(g.cover(key,
                                         [&](xrender::raster::Coverage& cov) {
                                             Path outline;
                                             outline.add_rounded_rect(c.rect, c.radii);
                                             xrender::raster::fill_coverage(outline.view(), m(), FillRule::nonzero, pixels(clips.back()), cov);
                                         }),
                                 0, 0, c.paint);
                            return;
                        }
                        shape(c.rect, c.radii, c.paint);
                    } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                        // Analytic, as a fill: the stroke's outer rrect less its inner (ui.slang's rring).
                        if (!(c.width > 0)) return;
                        const Ring ring = stroke_ring(c.rect, c.radii, c.width);
                        if (c.radii.smoothing > 0 && !c.radii.zero()) {
                            // Smoothed: the two outlines even-odd, through a mask, as the CPU fills them.
                            const auto key = device_key(10).rect(c.rect).radii(c.radii).f32(c.width).value();
                            if (rounded_instead(key, c.rect, c.radii, c.width, c.paint)) return;
                            mask(g.cover(key,
                                         [&](xrender::raster::Coverage& cov) {
                                             Path outline;
                                             outline.add_rounded_rect(ring.outer, ring.outer_radii);
                                             if (ring.hollow) outline.add_rounded_rect(ring.inner, ring.inner_radii);
                                             xrender::raster::fill_coverage(outline.view(), m(), FillRule::even_odd, pixels(clips.back()), cov);
                                         }),
                                 0, 0, c.paint);
                            return;
                        }
                        Draw d;
                        Prim& p = d.prim;
                        const Affine& mm = m();
                        int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
                        if (!paint_into(p, d, flags, c.paint, mm.map(ring.outer))) return;
                        clip_into(p, d, flags);
                        transform(p, mm, kind_rring, flags);
                        set(p.rect, ring.outer.x, ring.outer.y, ring.outer.width, ring.outer.height);
                        const CornerRadii& o = ring.outer_radii;
                        set(p.radii, o.top_left, o.top_right, o.bottom_right, o.bottom_left);
                        p.shape[0] = c.width;
                        emit(std::move(d));
                    } else if constexpr (std::same_as<T, BoxShadow>) {
                        const auto key = device_key(2).rect(c.rect).radii(c.radii).f32(c.offset.x).f32(c.offset.y).f32(c.blur).f32(c.spread).u64(c.inset).value();
                        if (shadow_instead(key, c)) return;
                        mask(g.cover(key,
                                     [&](xrender::raster::Coverage& cov) {
                                         xrender::raster::box_shadow_coverage(c.rect, c.radii, c.offset, c.blur, c.spread, c.inset, m(), pixels(clips.back()), cov,
                                                                     g.polygons, g.rasterizer);
                                     }),
                             0, 0, Paint::solid(c.color));
                    } else if constexpr (std::same_as<T, FillPath>) {
                        const auto key = device_key(3).path(c.path).u64(static_cast<std::uint64_t>(c.rule)).value();
                        if (c.rule == FillRule::nonzero && path_instead(key, c.path, c.paint)) return;
                        mask(g.cover(key,
                                     [&](xrender::raster::Coverage& cov) {
                                         xrender::raster::fill_coverage(c.path, m(), c.rule, pixels(clips.back()), cov);
                                     }),
                             0, 0, c.paint);
                    } else if constexpr (std::same_as<T, StrokePath>) {
                        Key k = device_key(4).path(c.path).f32(c.style.width).u64(static_cast<std::uint64_t>(c.style.cap));
                        k.u64(static_cast<std::uint64_t>(c.style.join)).f32(c.style.miter_limit).f32(c.style.dash_offset);
                        k.f32(c.style.trim_start).f32(c.style.trim_end).f32(c.style.trim_offset).bytes(std::as_bytes(std::span(c.style.dashes)));
                        mask(g.cover(k.value(),
                                     [&](xrender::raster::Coverage& cov) {
                                         xrender::raster::stroke_coverage(c.path,
                                                                 {.width = c.style.width,
                                                                  .cap = static_cast<xrender::raster::Cap>(c.style.cap),
                                                                  .join = static_cast<xrender::raster::Join>(c.style.join),
                                                                  .miter_limit = c.style.miter_limit,
                                                                  .dashes = c.style.dashes,
                                                                  .dash_offset = c.style.dash_offset,
                                                                  .trim_start = c.style.trim_start,
                                                                  .trim_end = c.style.trim_end,
                                                                  .trim_offset = c.style.trim_offset},
                                                                 m(), pixels(clips.back()), cov);
                                     }),
                             0, 0, c.paint);
                    } else if constexpr (std::same_as<T, DrawImage>) {
                        const xrender::raster::Surface* img = res.images ? res.images->get(c.image) : nullptr;
                        if (!img) {
                            ++report.missing_images;
                            return;
                        }
                        auto t = g.stored_texture(c.image.id, *img);
                        if (!t) {
                            fail(t.error());
                            return;
                        }
                        texture_image(*t, static_cast<std::uint32_t>(img->width()), static_cast<std::uint32_t>(img->height()), c.source, c.dest,
                                      c.sampling, c.opacity, m());
                    } else if constexpr (std::same_as<T, DrawGlyphs>) {
                        if (!res.glyphs) {
                            report.missing_glyphs += c.glyphs.size();
                            return;
                        }
                        if (keeping && kept_glyphs(c, index - in_run->first)) return;
                        const Affine& mm = m();
                        for (std::size_t i = 0; i < c.glyphs.size(); ++i) {
                            const Glyph& gl = c.glyphs[i];
                            const Placed pl = place(c, i, mm);
                            float opacity = 1;
                            Paint paint = c.paint;
                            if (pl.style) {
                                // The glyph's own opacity and tint over the run's colour (D-68).
                                opacity = std::clamp(pl.style->opacity, 0.0f, 1.0f);
                                if (opacity <= 0) continue;
                                if (c.paint.kind == Paint::Kind::solid) {
                                    const Color t = pl.style->tint;
                                    const float k = std::clamp(t.a, 0.0f, 1.0f);
                                    const Color col = c.paint.color;
                                    paint = Paint::solid({col.r + (t.r - col.r) * k, col.g + (t.g - col.g) * k, col.b + (t.b - col.b) * k, col.a * opacity});
                                }
                            }
                            // A single glyph's layer at an opacity is its coverage scaled.
                            const float scale = pl.style && c.paint.kind != Paint::Kind::solid ? opacity : 1.0f;
                            // No mask is ever made of a glyph (a host's frames): its outline at once, no key of a mask.
                            if (g.glyph_masks == 0) {
                                if (const Impl::Outlined* outline = g.outline_of(*res.glyphs, c.font, gl.id, g.scale_of(pl.outline))) {
                                    if (outline->count > 0) outline_glyph(*outline, pl.outline, paint, scale);
                                    continue;
                                }
                            }
                            if (pl.mask) {
                                const MaskCall call = mask_call(pl.pen, c.size, mm);
                                // The glyph cache's mask, kept in the atlas by what asked for it.
                                Key k;
                                k.u64(5).u64(c.font.id).u64(gl.id).f32(call.size).f32(call.fraction.x).f32(call.fraction.y);
                                const std::uint64_t key = k.value();
                                const auto hit = g.cached.find(key);
                                if (hit != g.cached.end() && !hit->second.color) {
                                    hit->second.last = g.frame_number;
                                    ++g.masks_reused;
                                    mask(&hit->second, static_cast<int>(call.ix), static_cast<int>(call.iy), paint, scale);
                                    continue;
                                }
                                if (hit == g.cached.end() && outlined_instead(key, c, gl.id, pl.outline, paint, scale)) continue;
                                const GlyphMask gm = res.glyphs->mask(c.font, gl.id, call.size, call.fraction);
                                if (gm.color) {
                                    if (hit == g.cached.end()) g.cached.emplace(key, Impl::Cached{.color = true, .last = g.frame_number});
                                    const auto& b = gm.color_bounds;
                                    image(*gm.color, {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                          {call.ix + b.x, call.iy + b.y, static_cast<float>(b.width), static_cast<float>(b.height)},
                                          Sampling::nearest, c.paint.color.a * opacity, Affine{});
                                } else if (gm.coverage) {
                                    mask(g.keep(key, *gm.coverage), static_cast<int>(call.ix), static_cast<int>(call.iy), paint, scale);
                                } else {
                                    ++report.missing_glyphs;
                                }
                            } else {
                                Key k;
                                k.u64(6).u64(c.font.id).u64(gl.id).affine(pl.outline).pixels(pixels(clips.back()));
                                if (!g.cached.contains(k.value()) && outlined_instead(k.value(), c, gl.id, pl.outline, paint, scale)) continue;
                                const xrender::raster::PathData outline = res.glyphs->outline(c.font, gl.id);
                                if (!outline.verbs.empty()) {
                                    mask(g.cover(k.value(),
                                                 [&](xrender::raster::Coverage& cov) {
                                                     xrender::raster::fill_coverage(outline, pl.outline, xrender::raster::FillRule::nonzero, pixels(clips.back()), cov);
                                                 }),
                                         0, 0, paint, scale);
                                } else {
                                    // A colour glyph (emoji): its image at device size, under the glyph's transform.
                                    const float s = mm.max_scale();
                                    const GlyphMask gm = res.glyphs->mask(c.font, gl.id, c.size * s, {0, 0});
                                    if (gm.color) {
                                        const auto& b = gm.color_bounds;
                                        const Affine to = mm * Affine::translate(pl.pen.x, pl.pen.y) * (pl.style ? pl.style->transform : Affine{}) *
                                                          Affine::scale(1 / s, 1 / s);
                                        image(*gm.color, {0, 0, static_cast<float>(b.width), static_cast<float>(b.height)},
                                              {static_cast<float>(b.x), static_cast<float>(b.y), static_cast<float>(b.width), static_cast<float>(b.height)},
                                              Sampling::linear, c.paint.color.a * opacity, to);
                                    } else {
                                        ++report.missing_glyphs;
                                    }
                                }
                            }
                        }
                    } else if constexpr (std::same_as<T, PushClipRect>) {
                        const Affine& mm = m();
                        if (c.radii.zero() && mm.b == 0 && mm.c == 0) {
                            const Rect r = mm.map(c.rect);
                            const Clip& below = clips.back();
                            clips.push_back({std::max(below.x0, r.x), std::max(below.y0, r.y), std::min(below.x1, r.right()), std::min(below.y1, r.bottom()),
                                             below.mask, below.coverage, below.key, below.record, below.depth, below.shape});
                        } else if (rounded_clip(c.rect, c.radii)) {
                            // (pushed: covered on the device from its outline)
                        } else {
                            Key k;
                            k.u64(7).affine(mm).rect(c.rect).radii(c.radii);
                            push_mask_clip(k, [&](xrender::raster::Coverage& cov) {
                                xrender::raster::PathBuilder pb;
                                xrender::raster::rounded_rect_path(pb, c.rect, c.radii);
                                xrender::raster::fill_coverage(pb.data(), mm, xrender::raster::FillRule::nonzero, whole(), cov);
                            });
                        }
                    } else if constexpr (std::same_as<T, PushClipPath>) {
                        Key k;
                        k.u64(8).affine(m()).path(c.path).u64(static_cast<std::uint64_t>(c.rule));
                        push_mask_clip(k, [&](xrender::raster::Coverage& cov) {
                            xrender::raster::fill_coverage(c.path, m(), c.rule, whole(), cov);
                        });
                    } else if constexpr (std::same_as<T, PopClip>) {
                        if (clips.size() > 1) clips.pop_back();
                    } else if constexpr (std::same_as<T, PushTransform>) {
                        transforms.push_back(m() * c.transform);
                    } else if constexpr (std::same_as<T, PopTransform>) {
                        if (transforms.size() > 1) transforms.pop_back();
                    } else if constexpr (std::same_as<T, PushLayer>) {
                        open_layer(c.opacity, c.blend);
                    } else if constexpr (std::same_as<T, PopLayer>) {
                        if (open.empty()) return;
                        const Open o = std::move(open.back());
                        open.pop_back();
                        if (o.effect) close_effect(o);
                        else close_layer(o);
                    } else if constexpr (std::same_as<T, ExternalTexture>) {
                        const std::optional<mlibs::rhi::Texture> t = options.external ? options.external(c.texture) : std::nullopt;
                        const auto desc = t ? g.d.describe(*t) : std::nullopt;
                        if (!desc || !mlibs::rhi::has(desc->usage, mlibs::rhi::TextureUsage::sampled)) {
                            ++report.external_skipped;
                            return;
                        }
                        const auto w = static_cast<float>(desc->width), h = static_cast<float>(desc->height);
                        texture_image(*t, desc->width, desc->height, {0, 0, w, h}, c.dest, c.sampling, c.opacity, m());
                    }
                },
                all[index]);
        }
        if (in_run) run_ends();
    };
    encode();
    if (!failed && g.atlas_overflow) {
        g.empty_atlas();
        g.pool.restart_frame();
        report = {};
        g.atlas_second_pass = true;
        encode();
        g.atlas_second_pass = false;
        report.atlas_emptied = true;
        if (!failed && g.atlas_overflow) fail({mlibs::rhi::Errc::out_of_memory, "the frame's coverage does not fit the atlas"});
    }
    if (!failed && g.too_wide)
        fail({mlibs::rhi::Errc::out_of_memory, std::format("a coverage mask {}×{} wider than the atlas", g.too_wide->width, g.too_wide->height)});
    g.too_wide.reset();
    if (failed) return std::unexpected(failure);

    // ── what the passes read: ramps, atlas, instances ────────────────────
    if (!g.ramp_rows.empty()) {
        if (auto r = g.ensure_ramps(static_cast<std::uint32_t>(g.ramp_rows.size())); !r) return std::unexpected(r.error());
        for (std::size_t i = 0; i < g.ramp_rows.size(); ++i)
            g.list.upload(g.ramps, mlibs::rhi::Region{0, static_cast<std::uint32_t>(i), 256, 1}, std::as_bytes(std::span(g.ramp_rows[i])));
    } else if (auto r = g.ensure_ramps(1); !r) {
        return std::unexpected(r.error());
    }
    if (auto o = g.ensure_outlines(); !o) return std::unexpected(o.error());
    if (g.outline_used > g.outline_sent) {
        // The rows the new points lie in (the first of them again, whole).
        const std::size_t first_row = g.outline_sent / Impl::outline_w, rows = g.outline_points.size() / Impl::outline_w - first_row;
        g.list.upload(g.outlines, mlibs::rhi::Region{0, static_cast<std::uint32_t>(first_row), Impl::outline_w, static_cast<std::uint32_t>(rows)},
                      std::as_bytes(std::span(g.outline_points).subspan(first_row * Impl::outline_w)));
        g.outline_sent = g.outline_used;
    }
    if (!g.clip_records.empty()) {
        // The frame's clips, in the store's first rows.
        std::ranges::copy(g.clip_records, g.outline_points.begin());
        const std::size_t rows = (g.clip_records.size() + Impl::outline_w - 1) / Impl::outline_w;
        g.list.upload(g.outlines, mlibs::rhi::Region{0, 0, Impl::outline_w, static_cast<std::uint32_t>(rows)},
                      std::as_bytes(std::span(g.outline_points).first(rows * Impl::outline_w)));
    }
    report.clips_outlined = g.clip_records.size() / Impl::clip_floats;
    std::size_t total = 0;
    for (const Pass& p : g.passes) total += p.draws.size();
    report.instances = total;
    if (total > 0) {
        const auto bytes = static_cast<std::uint32_t>(total * sizeof(Prim));
        if (bytes > g.instance_capacity) {
            if (g.instances) g.d.destroy(g.instances);
            std::uint32_t cap = std::max<std::uint32_t>(64 * sizeof(Prim), g.instance_capacity);
            while (cap < bytes) cap *= 2;
            auto b = g.d.create(mlibs::rhi::BufferDesc{cap, mlibs::rhi::BufferUsage::vertex, "render.instances"});
            if (!b) return std::unexpected(b.error());
            g.instances = *b;
            g.instance_capacity = cap;
        }
        g.prims.clear();
        for (const Pass& p : g.passes)
            for (const Draw& d : p.draws) g.prims.push_back(d.prim);
        g.list.upload(g.instances, 0, std::span<const Prim>(g.prims));
    }

    // ── the passes, through the frame's graph (D-81) ─────────────────────
    // Each pass says what it reads and writes; a layer nothing composites is
    // left out, and each runs in its trace span. Instances lie in the
    // passes' order whether or not a pass runs.
    std::vector<mlibs::rhi::Texture> textures;
    auto add = [&](mlibs::rhi::Texture t) {
        if (t && std::ranges::find(textures, t) == textures.end()) textures.push_back(t);
    };
    std::uint32_t first = 0;
    for (const Pass& s : g.passes) {
        const std::uint32_t base = first;
        first += static_cast<std::uint32_t>(s.draws.size());
        if (s.kind == Pass::Kind::effect) {
            const EffectStep& e = s.effect;
            xrender::PassDesc desc{.name = "render.vector.effect"};
            desc.reads = {e.source};
            if (e.backdrop && e.parent) desc.reads.push_back(e.parent);
            for (mlibs::rhi::Texture t : {e.result, e.aux, e.scratch})
                if (t) desc.writes.push_back(t);
            g.graph.pass(std::move(desc), [&g, &s](mlibs::rhi::CommandList&) { g.record_effect(s.effect); });
            continue;
        }
        if (s.kind == Pass::Kind::blend) {
            const BlendPass& b = s.blend;
            xrender::PassDesc desc{.name = "render.vector.blend", .reads = {s.target, b.layer}, .writes = {s.target, b.backdrop}};
            if (b.mask) desc.reads.push_back(b.mask);
            g.graph.pass(std::move(desc), [&g, &s](mlibs::rhi::CommandList& list) {
                const BlendPass& b = s.blend;
                const auto W = static_cast<float>(s.w), H = static_cast<float>(s.h);
                list.copy(s.target, b.box, b.backdrop, b.box.x, b.box.y);
                list.begin_render({s.target, mlibs::rhi::LoadOp::load, {}});
                const BlendParams params{{W, H, 0, 0},
                                         {static_cast<float>(b.box.x), static_cast<float>(b.box.y), static_cast<float>(b.box.x + b.box.width),
                                          static_cast<float>(b.box.y + b.box.height)},
                                         {static_cast<float>(b.mode), static_cast<float>(b.opacity), b.mask ? 1.0f : 0.0f, 0},
                                         b.clip};
                list.set_pipeline(g.blend_pipeline);
                list.push_uniforms(mlibs::rhi::Stage::vertex, 0, params);
                list.push_uniforms(mlibs::rhi::Stage::fragment, 0, params);
                list.bind_texture(mlibs::rhi::Stage::fragment, 0, b.layer, g.nearest);
                list.bind_texture(mlibs::rhi::Stage::fragment, 1, b.backdrop, g.nearest);
                list.bind_texture(mlibs::rhi::Stage::fragment, 2, b.mask ? b.mask : g.white_mask, g.nearest);
                list.draw(6);
                list.end_render();
            });
            continue;
        }
        // A pass that only loads and draws nothing does nothing.
        if (s.draws.empty() && s.load == mlibs::rhi::LoadOp::load) continue;
        textures.clear();
        if (s.load == mlibs::rhi::LoadOp::load) add(s.target);
        for (const Draw& d : s.draws) {
            add(d.image);
            add(d.mask);
            add(d.normal);
        }
        xrender::PassDesc desc{.name = "render.vector.draws", .reads = textures, .writes = {s.target}};
        g.graph.pass(std::move(desc), [&g, &s, base](mlibs::rhi::CommandList& list) {
            list.begin_render({s.target, s.load, s.clear});
            if (!s.draws.empty()) {
                const Frame frame{{static_cast<float>(s.w), static_cast<float>(s.h), static_cast<float>(g.atlas_w), static_cast<float>(g.atlas_h)},
                                  {static_cast<float>(g.ramp_capacity), static_cast<float>(Impl::outline_w), static_cast<float>(g.outline_rows), 0}};
                // The pipeline and what every draw binds; again when a run of lit
                // images begins or ends (ui_lit_fs).
                auto use = [&](mlibs::rhi::GraphicsPipeline pipeline) {
                    list.set_pipeline(pipeline);
                    list.push_uniforms(mlibs::rhi::Stage::vertex, 0, frame);
                    list.push_uniforms(mlibs::rhi::Stage::fragment, 0, frame);
                    list.bind_vertex_buffer(0, g.instances);
                    list.bind_texture(mlibs::rhi::Stage::fragment, 0, g.ramps, g.nearest);
                    if (!s.material_mix) {
                        list.bind_texture(mlibs::rhi::Stage::fragment, 1, g.atlas, g.nearest);
                        list.bind_texture(mlibs::rhi::Stage::fragment, 4, g.outlines, g.nearest);
                    }
                };
                use(s.material_mix ? g.material_pipeline : g.pipeline);
                bool lit = false;
                std::int32_t lit_pushed = -1;
                std::size_t i = 0;
                while (i < s.draws.size()) {
                    const Draw& head = s.draws[i];
                    std::size_t j = i + 1;
                    if (head.kept >= 0) {
                        // Glyphs kept on the device: their own instances, where their run lies this frame.
                        const Impl::KeptDraw& kept = g.kept_draws[static_cast<std::size_t>(head.kept)];
                        if (kept.run->buffer) {
                            list.set_pipeline(g.kept_pipeline);
                            list.push_uniforms(mlibs::rhi::Stage::vertex, 0, frame);
                            list.push_uniforms(mlibs::rhi::Stage::vertex, 1, kept.uniforms);
                            list.push_uniforms(mlibs::rhi::Stage::fragment, 0, frame);
                            list.bind_vertex_buffer(0, kept.run->buffer);
                            list.bind_texture(mlibs::rhi::Stage::fragment, 0, g.ramps, g.nearest);
                            list.bind_texture(mlibs::rhi::Stage::fragment, 1, g.atlas, g.nearest);
                            list.bind_texture(mlibs::rhi::Stage::fragment, 2, g.white, g.nearest);
                            list.bind_texture(mlibs::rhi::Stage::fragment, 3, head.mask ? head.mask : g.white_mask, g.nearest);
                            list.bind_texture(mlibs::rhi::Stage::fragment, 4, g.outlines, g.nearest);
                            list.draw(6, kept.count, 0, kept.first);
                            lit = false;
                            lit_pushed = -1;
                            use(g.pipeline);
                        }
                        i = j;
                        continue;
                    }
                    while (j < s.draws.size() && s.draws[j].kept < 0 && s.draws[j].image == head.image && s.draws[j].mask == head.mask &&
                           s.draws[j].lit == head.lit && s.draws[j].normal == head.normal)
                        ++j;
                    if ((head.lit >= 0) != lit) {
                        lit = head.lit >= 0;
                        use(s.material_mix ? (lit ? g.material_lit_pipeline : g.material_pipeline) : (lit ? g.lit_pipeline : g.pipeline));
                        lit_pushed = -1;
                    }
                    if (lit) {
                        if (head.lit != lit_pushed) list.push_uniforms(mlibs::rhi::Stage::fragment, 1, g.lits[static_cast<std::size_t>(head.lit)]);
                        lit_pushed = head.lit;
                        list.bind_texture(mlibs::rhi::Stage::fragment, s.material_mix ? 2 : 5, head.normal, g.nearest);
                    }
                    list.bind_texture(mlibs::rhi::Stage::fragment, s.material_mix ? 1 : 2, head.image ? head.image : g.white, g.nearest);
                    if (!s.material_mix) list.bind_texture(mlibs::rhi::Stage::fragment, 3, head.mask ? head.mask : g.white_mask, g.nearest);
                    list.draw(6, static_cast<std::uint32_t>(j - i), 0, base + static_cast<std::uint32_t>(i));
                    i = j;
                }
            }
            list.end_render();
        });
    }
    auto recorded = g.graph.record(g.list, std::span(&target, 1));
    if (!recorded) {
        std::string why;
        for (const xrender::GraphProblem& p : recorded.error()) why += (why.empty() ? "" : "; ") + p.text;
        return mlibs::rhi::fail(mlibs::rhi::Errc::invalid, "render: the frame's passes: " + why);
    }
    report.gpu_passes = recorded->recorded;
    report.gpu_passes_culled = recorded->culled;
    for (const mlibs::rhi::Command& c : g.list.commands()) {
        if (const auto* u = std::get_if<mlibs::rhi::cmd::UploadTexture>(&c)) report.uploaded += u->data.size();
        else if (const auto* b = std::get_if<mlibs::rhi::cmd::UploadBuffer>(&c)) report.uploaded += b->data.size();
    }
    auto submitted = g.d.submit(g.list);
    for (mlibs::rhi::Texture t : g.retired) g.d.destroy(t);  // safe once submitted: released when the GPU is done
    g.retired.clear();
    if (!submitted) return std::unexpected(submitted.error());
    report.rows = g.height;
    report.masks_made = g.masks_made;
    report.masks_reused = g.masks_reused;
    report.glyphs_outlined = g.glyphs_outlined;
    report.outline_points = (g.outline_used - outline_before) / 2;
    if (g.glyph_masks != std::numeric_limits<std::size_t>::max()) {
        g.asked_before.swap(g.asked);
        std::ranges::sort(g.asked_before);
    }
    return report;
}

}  // namespace xrender
