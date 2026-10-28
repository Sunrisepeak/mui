module openmui.render.vector;

import std;
import openmui.geom;
import openmui.raster;
import openmui.rhi;
import :shared;

namespace omui::render {

namespace {

namespace ui = shaders::ui;
namespace blend = shaders::blend;

// One instance of the shader's quad (shaders/ui.slang, struct Prim).
struct Prim {
    std::array<float, 4> rect{}, m0{}, m1{}, radii{}, shape{}, color{}, paint{}, pgeo{}, pinv0{}, pinv1{}, clip{}, uv{};
};
static_assert(sizeof(Prim) == 12 * 16);

constexpr float kind_rect = 0, kind_rrect = 1, kind_mask = 2, kind_image = 3, kind_rring = 4;
constexpr int flag_axis = 1, flag_clip_mask = 2, flag_linear = 4, flag_layer = 8;

// A value 0…1 as a byte, the way raster rounds opacities.
float byte_of(float v) { return static_cast<float>(static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); }

void edges(std::array<float, 4>& v, const Rect& r) { v = {r.x, r.y, r.right(), r.bottom()}; }

struct Draw {
    Prim         prim;
    rhi::Texture image;  // the image slot's texture (none: white)
    rhi::Texture mask;   // the clip mask (none: white)
};

// A layer composited under a blend mode other than normal (shaders/
// blend.slang): the target's box is copied to the backdrop, and the layer
// blended with it is written in its place.
struct BlendPass {
    rhi::Texture         layer;
    rhi::Texture         backdrop;
    rhi::Region          box;
    BlendMode            mode = BlendMode::normal;
    std::uint32_t        opacity = 255;  // 0…255, as the CPU rounds it
    rhi::Texture         mask;           // the clip mask, or none
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
    rhi::GraphicsPipeline pipeline;  // pixel: the effect's
    EffectUniforms        uniforms;
    rhi::Texture          source, scratch, result, aux;
    // An effect that reads its backdrop: what the parent target holds under
    // the surface — the same when the layer ends as when it began, since its
    // content is drawn elsewhere — copied into `aux` and frosted.
    bool                  backdrop = false;
    rhi::Texture          parent;
    std::int32_t          x0 = 0, y0 = 0;  // the surface's corner in the parent
    std::uint32_t         parent_w = 0, parent_h = 0;
    int                   frost = 0;       // box radius, pixels
};

// The frame's work, in the order it runs: a layer's passes run before the
// rest of its parent's.
struct Pass {
    enum class Kind : std::uint8_t { draws, blend, effect };
    Kind                 kind = Kind::draws;
    rhi::Texture         target;
    std::uint32_t        w = 0, h = 0;
    rhi::LoadOp          load = rhi::LoadOp::load;
    std::array<float, 4> clear{};
    std::vector<Draw>    draws;   // draws
    BlendPass            blend;   // blend
    EffectStep           effect;  // effect
};

struct Frame {
    std::array<float, 4> size;   // target w, h; atlas w, h
    std::array<float, 4> ramps;  // ramp rows
};

struct Clip {
    float        x0, y0, x1, y1;  // the device rectangle
    rhi::Texture mask;           // the mask texture, or none
    std::shared_ptr<std::vector<std::uint8_t>> coverage;  // the mask, surface-sized
    std::uint64_t key = 0;       // what made the mask (0: none)
};

// A colour premultiplied to bytes as raster does it, as exact fractions of 255.
std::array<float, 4> premultiplied(Color c) {
    const raster::Rgba8 b = raster::premultiply(c);
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

raster::PixelRect pixels(const Clip& c) {
    const auto x0 = static_cast<std::int32_t>(std::floor(c.x0)), y0 = static_cast<std::int32_t>(std::floor(c.y0));
    const auto x1 = static_cast<std::int32_t>(std::ceil(c.x1)), y1 = static_cast<std::int32_t>(std::ceil(c.y1));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

// What the effect template allows a shader to bind: the source and the aux
// texture, the uniforms; no storage (architecture §8).
bool template_shaped(const rhi::ShaderDesc& s) {
    // source, and aux (rows, or the backdrop); the uniforms
    return s.stage == rhi::Stage::fragment && s.samplers <= 2 && s.uniform_buffers <= 1 && s.storage_textures == 0 && s.storage_buffers == 0 &&
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
    Key& radii(const CornerRadii& c) noexcept { return f32(c.top_left).f32(c.top_right).f32(c.bottom_right).f32(c.bottom_left); }
    Key& affine(const Affine& m) noexcept { return f32(m.a).f32(m.b).f32(m.c).f32(m.d).f32(m.e).f32(m.f); }
    Key& pixels(const raster::PixelRect& r) noexcept { return i32(r.x).i32(r.y).i32(r.width).i32(r.height); }
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
    rhi::Device&          d;
    rhi::GraphicsPipeline pipeline, blend_pipeline;
    rhi::Sampler          nearest;  // every texture is read texel by texel; bilinear images are weighed in the shader
    rhi::Texture          white, white_mask, zero_row;
    rhi::Texture          ramps;
    std::uint32_t         ramp_capacity = 0;
    // The coverage atlas, kept from frame to frame: masks by what made them
    // (Key), placed on shelves 2048 wide; it grows to the device's limit, and
    // when even that is full it is emptied and the frame encoded again.
    rhi::Texture          atlas;
    std::uint32_t         atlas_w = 4096, atlas_h = 0;  // a 4K target's widest mask fits
    struct Cached {
        std::uint32_t     x = 0, y = 0;        // in the atlas
        raster::PixelRect bounds;              // where the mask lies, as rasterised
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
    std::optional<raster::PixelRect> too_wide;     // a mask wider than the atlas (an error)
    std::vector<rhi::Texture> retired;             // atlases grown out of, destroyed after the submission
    // Rounded and path clips' masks, surface-sized, by what made them.
    struct ClipMask {
        rhi::Texture                               texture;
        std::shared_ptr<std::vector<std::uint8_t>> coverage;
        std::uint64_t                              last = 0;
    };
    std::unordered_map<std::uint64_t, ClipMask> clip_masks;
    rhi::Buffer           instances;
    std::uint32_t         instance_capacity = 0;
    std::unordered_map<const raster::Surface*, rhi::Texture> images;  // surfaces without an id (glyph colour images)
    // The ImageStore's images by id: immutable while the store holds them, so
    // uploaded once (a wall of thumbnails costs its uploads once, not a frame).
    struct Stored {
        rhi::Texture           texture;
        const raster::Surface* surface = nullptr;
    };
    std::unordered_map<std::uint32_t, Stored> stored;
    // Textures of images the store let go, kept by size for the next image
    // of that size (a canvas's tiers come and go every frame of a zoom: no
    // texture made or destroyed for them); one unused for 120 frames goes.
    struct Spare {
        rhi::Texture  texture;
        std::uint32_t w, h;
        std::uint64_t last;
    };
    std::vector<Spare> spares;

    // The effects' pipelines, made on first use: a pixel effect's by its ref
    // and generation (a reloaded shader is a new pipeline).
    rhi::GraphicsPipeline blur_pipeline, particles_pipeline;
    rhi::ComputePipeline  pixelate_pipeline;
    std::map<std::pair<std::uint32_t, std::uint64_t>, rhi::GraphicsPipeline> effect_pipelines;

    // The frame's transient textures (layers, backdrops, effect surfaces):
    // the pool's, reused from frame to frame (D-81); the graph the frame's
    // passes are recorded through.
    render::TexturePool pool;
    render::Graph       graph{pool};
    std::uint64_t       frame_number = 0;

    std::array<rhi::VertexAttribute, 12> attributes{};
    std::array<rhi::VertexBufferLayout, 1> layout{};

    // The frame being built.
    std::uint32_t                       width = 0, height = 0;
    std::vector<Pass>                   passes;
    std::vector<std::array<raster::Rgba8, 256>> ramp_rows;
    raster::Coverage                    scratch;  // a mask being made (uploaded, then reused)
    std::deque<std::vector<float>>      rows;   // the frame's effect rows (uploads point into them)
    std::vector<const raster::Surface*> images_used;
    std::vector<Prim>                   prims;  // the frame's instances, all passes'
    rhi::CommandList                    list;
    raster::Polygons                    polygons;
    raster::Rasterizer                  rasterizer;

    explicit Impl(rhi::Device& device) : d(device), pool(device) {
        for (std::uint32_t i = 0; i < 12; ++i) attributes[i] = {i, rhi::VertexFormat::float4, i * 16};
        layout[0] = {sizeof(Prim), true, attributes};
    }
    ~Impl() {
        static_cast<void>(d.wait_idle());
        for (auto& [s, t] : images) d.destroy(t);
        for (auto& [id, st] : stored) d.destroy(st.texture);
        for (const Spare& sp : spares) d.destroy(sp.texture);
        for (auto& [k, c] : clip_masks) d.destroy(c.texture);
        for (rhi::Texture t : retired) d.destroy(t);
        for (rhi::Texture t : {white, white_mask, zero_row, ramps, atlas}) d.destroy(t);
        d.destroy(instances);
        d.destroy(nearest);
        for (rhi::GraphicsPipeline p : {pipeline, blend_pipeline, blur_pipeline, particles_pipeline}) d.destroy(p);
        for (auto& [k, p] : effect_pipelines) d.destroy(p);
        d.destroy(pixelate_pipeline);
    }

    rhi::Result<void> init() {
        auto p = d.create(rhi::GraphicsPipelineDesc{.vertex = &ui::ui_vs, .fragment = &ui::ui_fs, .vertex_buffers = layout,
                                                    .blend = rhi::Blend::premultiplied(), .label = "render.ui"});
        if (!p) return std::unexpected(p.error());
        pipeline = *p;
        auto bp = d.create(rhi::GraphicsPipelineDesc{.vertex = &blend::blend_vs, .fragment = &blend::blend_fs, .blend = rhi::Blend::replace(),
                                                     .label = "render.blend"});
        if (!bp) return std::unexpected(bp.error());
        blend_pipeline = *bp;
        auto n = d.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
        if (!n) return std::unexpected(n.error());
        nearest = *n;
        auto w = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 1, 1, rhi::TextureUsage::sampled, "render.white"});
        auto wm = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, 1, 1, rhi::TextureUsage::sampled, "render.white_mask"});
        auto zr = d.create(rhi::TextureDesc{rhi::Format::r32_float, 1, 1, rhi::TextureUsage::sampled, "render.zero_row"});
        if (!w || !wm || !zr) return std::unexpected(!w ? w.error() : !wm ? wm.error() : zr.error());
        white = *w;
        white_mask = *wm;
        zero_row = *zr;
        rhi::CommandList up;
        const std::array<std::uint8_t, 4> one{255, 255, 255, 255};
        const std::array<float, 1> zero{0};
        up.upload(white, rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one)));
        up.upload(white_mask, rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(one).first(1)));
        up.upload(zero_row, rhi::Region{0, 0, 1, 1}, std::as_bytes(std::span(zero)));
        auto s = d.submit(up);
        if (!s) return std::unexpected(s.error());
        return d.wait(*s);
    }

    // ── per-frame resources ──────────────────────────────────────────────

    // A texture for this frame only: the graph's, from the pool.
    rhi::Result<rhi::Texture> pooled(std::uint32_t w, std::uint32_t h, rhi::Format format, rhi::TextureUsage usage, std::string_view label) {
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
    }

    rhi::Result<void> ensure_ramps(std::uint32_t rows) {
        if (rows <= ramp_capacity && ramps) return {};
        std::uint32_t cap = std::max<std::uint32_t>(16, ramp_capacity);
        while (cap < rows) cap *= 2;
        if (ramps) d.destroy(ramps);
        auto t = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, 256, cap, rhi::TextureUsage::sampled, "render.ramps"});
        if (!t) return std::unexpected(t.error());
        ramps = *t;
        ramp_capacity = cap;
        return {};
    }

    // The atlas exists (1024 rows to begin with).
    rhi::Result<void> ensure_atlas() {
        if (atlas) return {};
        auto t = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, atlas_w, 1024, rhi::TextureUsage::sampled, "render.atlas"});
        if (!t) return std::unexpected(t.error());
        atlas = *t;
        atlas_h = 1024;
        return {};
    }

    // Twice the rows, what it held copied over (in the list, after the
    // uploads before it); false at the device's limit.
    bool grow_atlas() {
        const std::uint32_t cap = atlas_h * 2;
        if (cap > d.caps().max_texture_size) return false;
        auto t = d.create(rhi::TextureDesc{rhi::Format::r8_unorm, atlas_w, cap, rhi::TextureUsage::sampled, "render.atlas"});
        if (!t) return false;
        const std::uint32_t used = std::min(atlas_h, shelf_y + shelf_h);
        if (used > 0) list.copy(atlas, rhi::Region{0, 0, atlas_w, used}, *t, 0, 0);
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
    void upload_surface(rhi::Texture t, const raster::Surface& s, bool borrowed) {
        if (s.width() <= 0 || s.height() <= 0) return;
        const rhi::Region all{0, 0, static_cast<std::uint32_t>(s.width()), static_cast<std::uint32_t>(s.height())};
        if (borrowed) list.upload_borrowed(t, all, std::as_bytes(s.pixels()));
        else list.upload(t, all, std::as_bytes(s.pixels()));
    }

    // An ImageStore image: uploaded the first time it is drawn.
    rhi::Result<rhi::Texture> stored_texture(std::uint32_t id, const raster::Surface& s) {
        if (auto it = stored.find(id); it != stored.end() && it->second.surface == &s) return it->second.texture;
        if (auto it = stored.find(id); it != stored.end()) {
            spare(it->second);
            stored.erase(it);
        }
        const auto w = static_cast<std::uint32_t>(s.width()), h = static_cast<std::uint32_t>(s.height());
        rhi::Texture t{};
        if (auto it = std::ranges::find_if(spares, [&](const Spare& sp) { return sp.w == w && sp.h == h; }); it != spares.end()) {
            t = it->texture;
            *it = spares.back();
            spares.pop_back();
        } else {
            auto made = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, w, h, rhi::TextureUsage::sampled, "render.image"});
            if (!made) return std::unexpected(made.error());
            t = *made;
        }
        upload_surface(t, s, true);
        stored.emplace(id, Stored{t, &s});
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
            if (store && store->get(ImageRef{kv.first}) == kv.second.surface) return false;
            spare(kv.second);
            return true;
        });
    }

    rhi::Result<rhi::Texture> image_texture(const raster::Surface& s) {
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
            auto t = d.create(rhi::TextureDesc{rhi::Format::rgba8_unorm, static_cast<std::uint32_t>(s.width()),
                                               static_cast<std::uint32_t>(s.height()), rhi::TextureUsage::sampled, "render.image"});
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

    const Cached* keep(std::uint64_t key, const raster::Coverage& c) {
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
        list.upload(atlas, rhi::Region{e.x, e.y, w, h}, std::as_bytes(std::span(c.values)));
        return &cached.emplace(key, e).first->second;
    }

    // ── the effects' pipelines ───────────────────────────────────────────

    rhi::Result<rhi::GraphicsPipeline> effect_pipeline(EffectRef ref, std::uint64_t generation, const rhi::ShaderDesc& fragment) {
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
        auto p = d.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::quad::quad_vs, .fragment = &fragment, .blend = rhi::Blend::replace(),
                                                    .label = "render.effect"});
        if (!p) return std::unexpected(p.error());
        effect_pipelines.emplace(key, *p);
        return *p;
    }

    rhi::Result<void> builtin_pipelines(EffectPass pass) {
        if (pass == EffectPass::blur && !blur_pipeline) {
            auto p = d.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::quad::quad_vs, .fragment = &shaders::blur::blur_fs,
                                                        .blend = rhi::Blend::replace(), .label = "render.effect.blur"});
            if (!p) return std::unexpected(p.error());
            blur_pipeline = *p;
        }
        if (pass == EffectPass::particles && !particles_pipeline) {
            auto p = d.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::particles::particles_vs, .fragment = &shaders::particles::particles_fs,
                                                        .blend = rhi::Blend::premultiplied(), .label = "render.effect.particles"});
            if (!p) return std::unexpected(p.error());
            particles_pipeline = *p;
        }
        if (pass == EffectPass::pixelate && !pixelate_pipeline) {
            auto p = d.create(rhi::ComputePipelineDesc{&shaders::pixelate::pixelate_cs, "render.effect.pixelate"});
            if (!p) return std::unexpected(p.error());
            pixelate_pipeline = *p;
        }
        return {};
    }

    // The commands that run an effect step (outside any pass).
    void record_effect(const EffectStep& e) {
        const auto w = static_cast<std::int32_t>(e.uniforms.size[0]), h = static_cast<std::int32_t>(e.uniforms.size[1]);
        auto quad = [&](rhi::GraphicsPipeline p, rhi::Texture from, rhi::Texture to, const EffectUniforms& u, rhi::Texture aux) {
            list.begin_render({to, rhi::LoadOp::clear, {0, 0, 0, 0}});
            list.set_pipeline(p);
            list.push_uniforms(rhi::Stage::fragment, 0, u);
            list.bind_texture(rhi::Stage::fragment, 0, from, nearest);
            if (aux) list.bind_texture(rhi::Stage::fragment, 1, aux, nearest);
            list.draw(3);
            list.end_render();
        };
        if (e.backdrop) {
            // The backdrop: transparent, then what the parent holds under the surface, frosted.
            list.begin_render({e.aux, rhi::LoadOp::clear, {0, 0, 0, 0}});
            list.end_render();
            const std::int32_t cx0 = std::max(e.x0, 0), cy0 = std::max(e.y0, 0);
            const std::int32_t cx1 = std::min(e.x0 + w, static_cast<std::int32_t>(e.parent_w)), cy1 = std::min(e.y0 + h, static_cast<std::int32_t>(e.parent_h));
            if (cx1 > cx0 && cy1 > cy0)
                list.copy(e.parent,
                          rhi::Region{static_cast<std::uint32_t>(cx0), static_cast<std::uint32_t>(cy0), static_cast<std::uint32_t>(cx1 - cx0),
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
                quad(e.pipeline, e.source, e.result, e.uniforms, e.gpu.fragment->samplers >= 2 ? (e.aux ? e.aux : zero_row) : rhi::Texture{});
                break;
            case EffectPass::blur: {
                // Three box passes each way, as the reference: source → scratch → result → scratch → … → result.
                EffectUniforms u = e.uniforms;
                rhi::Texture from = e.source;
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
                const PixelateGrid grid{{w, h, size, 0}, {fx, fy, across, down}};
                const std::array<rhi::Texture, 1> writes{e.result};
                list.begin_compute(writes);
                list.set_pipeline(pixelate_pipeline);
                list.bind_texture(rhi::Stage::compute, 0, e.source, nearest);
                list.push_uniforms(rhi::Stage::compute, 0, grid);
                list.dispatch(static_cast<std::uint32_t>((across + 7) / 8), static_cast<std::uint32_t>((down + 7) / 8));
                list.end_compute();
                break;
            }
            case EffectPass::particles: {
                const auto size = static_cast<std::int32_t>(e.uniforms.derived[0]);
                const auto x0 = static_cast<std::int32_t>(e.uniforms.derived[1]), y0 = static_cast<std::int32_t>(e.uniforms.derived[2]);
                const auto x1 = static_cast<std::int32_t>(e.uniforms.derived[3]), y1 = static_cast<std::int32_t>(e.uniforms.derived[4]);
                const std::int32_t across = (x1 - x0 + size - 1) / size, down = (y1 - y0 + size - 1) / size;
                list.begin_render({e.result, rhi::LoadOp::clear, {0, 0, 0, 0}});
                if (across > 0 && down > 0) {
                    list.set_pipeline(particles_pipeline);
                    list.push_uniforms(rhi::Stage::vertex, 0, e.uniforms);
                    list.push_uniforms(rhi::Stage::fragment, 0, e.uniforms);
                    list.bind_texture(rhi::Stage::fragment, 0, e.source, nearest);
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

rhi::Device& GpuRenderer::device() const noexcept { return impl_->d; }

rhi::ShaderFormats gpu_shader_formats() noexcept { return ui::ui_vs.formats(); }

rhi::Result<GpuRenderer> GpuRenderer::create(rhi::Device& device) {
    auto impl = std::make_unique<Impl>(device);
    if (auto r = impl->init(); !r) return std::unexpected(r.error());
    return GpuRenderer(std::move(impl));
}

rhi::Result<RenderReport> GpuRenderer::render(const DisplayList& dl, rhi::Texture target, const Resources& res, const GpuOptions& options) {
    Impl& g = *impl_;
    const auto desc = g.d.describe(target);
    if (!desc) return rhi::fail(rhi::Errc::invalid, "render: a stale target");
    if (desc->format != rhi::Format::rgba8_unorm || !rhi::has(desc->usage, rhi::TextureUsage::color_target))
        return rhi::fail(rhi::Errc::invalid, "render: the target must be an rgba8_unorm colour target");
    g.width = desc->width;
    g.height = desc->height;

    RenderReport report;
    g.pool_frame();
    g.release_stored(res.images);
    g.images_used.clear();
    g.list.clear();
    if (auto a = g.ensure_atlas(); !a) return std::unexpected(a.error());
    rhi::Error failure{};
    bool failed = false;
    auto fail = [&](const rhi::Error& e) {
        if (!failed) failure = e, failed = true;
    };
    auto check = [&](const auto& r) {
        if (!r) fail(r.error());
        return static_cast<bool>(r);
    };

    // The frame, encoded: the passes and their draws. Run again, once, with
    // the atlas emptied when this frame's masks did not fit what it held.
    auto encode = [&] {
        g.passes.clear();
        g.graph.clear();
        g.ramp_rows.clear();
        g.rows.clear();
        g.atlas_overflow = false;
        g.masks_made = g.masks_reused = 0;
        // The surface drawn into: the target, or an effect layer's own.
        struct Surface {
            std::uint32_t w, h;
        };
        std::vector<Surface> surfaces{{g.width, g.height}};
        auto here = [&]() -> const Surface& { return surfaces.back(); };
        auto whole = [&]() -> raster::PixelRect { return {0, 0, static_cast<std::int32_t>(here().w), static_cast<std::int32_t>(here().h)}; };

        std::size_t current = 0;
        auto draws_into = [&](rhi::Texture t, std::uint32_t w, std::uint32_t h, rhi::LoadOp load, std::array<float, 4> clear) {
            Pass p;
            p.target = t;
            p.w = w;
            p.h = h;
            p.load = load;
            p.clear = clear;
            g.passes.push_back(std::move(p));
            current = g.passes.size() - 1;
        };
        draws_into(target, g.width, g.height, options.clear ? rhi::LoadOp::clear : rhi::LoadOp::load, premultiplied(options.background));

        std::vector<Affine> transforms{Affine::scale(options.scale, options.scale)};
        std::vector<Clip> clips{{0, 0, static_cast<float>(g.width), static_cast<float>(g.height), {}, nullptr}};
        // An open layer: an ordinary one (drawn into a surface-sized texture), or
        // an effect's (drawn into its own surface, the transform, clip and
        // surface pushed with it).
        struct Open {
            std::size_t  parent;
            rhi::Texture texture;
            float        opacity;
            BlendMode    blend;
            bool         effect = false;
            EffectStep   step;
            Rect         device;  // effect: where its surface lies in the parent's
        };
        std::vector<Open> open;
        PathConv paths;
        std::vector<raster::Stop> stops;
        Path scratch_path;

        auto m = [&]() -> const Affine& { return transforms.back(); };
        auto clip_into = [&](Prim& p, Draw& d, int& flags) {
            const Clip& c = clips.back();
            set(p.clip, c.x0, c.y0, c.x1, c.y1);
            if (c.mask) {
                flags |= flag_clip_mask;
                d.mask = c.mask;
            }
        };
        // The paint's colour fields; false when it cannot be drawn (a missing image).
        // The shader works in the CPU's bytes: a solid colour premultiplied as
        // raster premultiplies it, an image paint's tint as a byte.
        auto paint_into = [&](Prim& p, Draw& d, int& flags, const Paint& paint) -> bool {
            p.shape[1] = 255;
            switch (paint.kind) {
                case Paint::Kind::solid: {
                    p.color = premultiplied(paint.color);
                    p.paint = {0, 0, 0, 0};
                    return true;
                }
                case Paint::Kind::gradient: {
                    raster::Paint rp;
                    rp.kind = raster::Paint::Kind::gradient;
                    rp.gradient = static_cast<raster::GradientKind>(paint.gradient.kind);
                    rp.start = paint.gradient.start;
                    rp.end = paint.gradient.end;
                    rp.radius = paint.gradient.radius;
                    rp.spread = static_cast<raster::Spread>(paint.gradient.spread);
                    stops.clear();
                    for (const GradientStop& s : paint.gradient.stops) stops.push_back({s.offset, s.color});
                    rp.stops = stops;
                    const raster::Shader shader(rp);
                    std::array<raster::Rgba8, 256> row{};
                    std::ranges::copy(shader.table(), row.begin());
                    g.ramp_rows.push_back(row);
                    const float kind = paint.gradient.kind == GradientKind::linear ? 1.0f : paint.gradient.kind == GradientKind::radial ? 2.0f : 3.0f;
                    set(p.paint, kind, static_cast<float>(g.ramp_rows.size() - 1), static_cast<float>(paint.gradient.spread), paint.gradient.radius);
                    if (paint.gradient.kind == GradientKind::radial)
                        set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.radius, 0);
                    else
                        set(p.pgeo, paint.gradient.start.x, paint.gradient.start.y, paint.gradient.end.x, paint.gradient.end.y);
                    inverse(p, m());
                    return true;
                }
                case Paint::Kind::image: {
                    const raster::Surface* img = res.images ? res.images->get(paint.image) : nullptr;
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
                    p.color = {0, 0, 0, byte_of(paint.color.a)};
                    p.paint = {4, 0, 0, 0};
                    inverse(p, m() * paint.image_transform);
                    p.pinv1[2] = static_cast<float>(img->width());
                    p.pinv1[3] = static_cast<float>(img->height());
                    return true;
                }
            }
            return false;
        };
        auto emit = [&](Draw d) { g.passes[current].draws.push_back(std::move(d)); };
        // An analytic rectangle or rounded rectangle.
        auto shape = [&](const Rect& r, const CornerRadii& radii, const Paint& paint) {
            Draw d;
            Prim& p = d.prim;
            const Affine& mm = m();
            const bool rounded = !radii.zero();
            int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
            if (!paint_into(p, d, flags, paint)) return;
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
            if (!paint_into(p, d, flags, paint)) return;
            p.shape[1] = byte_of(scale);  // a layered glyph's opacity, applied after, as its layer's pop
            clip_into(p, d, flags);
            transform(p, Affine{}, kind_mask, flags);
            set(p.rect, static_cast<float>(e->bounds.x + dx), static_cast<float>(e->bounds.y + dy), static_cast<float>(e->bounds.width),
                static_cast<float>(e->bounds.height));
            set(p.uv, static_cast<float>(e->x), static_cast<float>(e->y), 0, 0);
            emit(std::move(d));
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
        auto texture_image = [&](rhi::Texture t, std::uint32_t w, std::uint32_t h, const Rect& source, const Rect& dest, Sampling sampling,
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
        auto layer_image = [&](rhi::Texture t, std::uint32_t w, std::uint32_t h, float opacity) {
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
        auto image = [&](const raster::Surface& img, const Rect& source, const Rect& dest, Sampling sampling, float opacity, const Affine& mm) {
            auto t = g.image_texture(img);
            if (!t) {
                fail(t.error());
                return;
            }
            texture_image(*t, static_cast<std::uint32_t>(img.width()), static_cast<std::uint32_t>(img.height()), source, dest, sampling, opacity, mm);
        };
        // A clip by a mask, surface-sized, under the clip below it: made
        // (`make` rasterises it) and uploaded only when nothing made the same.
        auto push_mask_clip = [&](Key key, auto&& make) {
            const Clip& below = clips.back();
            const std::uint32_t W = here().w, H = here().h;
            const std::uint64_t k = key.u64(W).u64(H).u64(below.key).value();
            if (auto it = g.clip_masks.find(k); it != g.clip_masks.end()) {
                it->second.last = g.frame_number;
                clips.push_back({below.x0, below.y0, below.x1, below.y1, it->second.texture, it->second.coverage, k});
                return;
            }
            raster::Coverage& c = g.scratch;
            c.bounds = {};
            c.values.clear();
            make(c);
            auto values = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(W) * H, 0);
            for (std::int32_t y = std::max(0, c.bounds.y); y < std::min<std::int32_t>(static_cast<std::int32_t>(H), c.bounds.y + c.bounds.height); ++y)
                for (std::int32_t x = std::max(0, c.bounds.x); x < std::min<std::int32_t>(static_cast<std::int32_t>(W), c.bounds.x + c.bounds.width); ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x);
                    const std::uint32_t under = below.coverage ? (*below.coverage)[i] : 255u;
                    (*values)[i] = static_cast<std::uint8_t>(raster::mul255(c.at(x, y), under));
                }
            auto t = g.d.create(rhi::TextureDesc{rhi::Format::r8_unorm, W, H, rhi::TextureUsage::sampled, "render.clip_mask"});
            if (!t) {
                fail(t.error());
                clips.push_back(below);
                return;
            }
            g.list.upload(*t, rhi::Region{0, 0, W, H}, std::as_bytes(std::span(*values)));
            g.clip_masks.emplace(k, Impl::ClipMask{*t, values, g.frame_number});
            clips.push_back({below.x0, below.y0, below.x1, below.y1, *t, std::move(values), k});
        };
        // An ordinary layer opens: drawn into a surface-sized texture.
        auto open_layer = [&](float opacity, BlendMode mode) {
            auto t = g.pooled(here().w, here().h, rhi::Format::rgba8_unorm, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "render.layer");
            if (!t) {
                fail(t.error());
                return;
            }
            open.push_back({current, *t, opacity, mode});
            draws_into(*t, here().w, here().h, rhi::LoadOp::clear, {0, 0, 0, 0});
        };
        // An ordinary layer closes: over its parent, within the clip and under its
        // mask as the CPU composites it (what it holds was clipped as it was
        // drawn, and is masked again), or blended with what lies under it.
        auto close_layer = [&](const Open& o) {
            const rhi::Texture parent = g.passes[o.parent].target;
            const std::uint32_t W = g.passes[o.parent].w, H = g.passes[o.parent].h;
            const Clip& c = clips.back();
            if (o.blend != BlendMode::normal) {
                raster::PixelRect b = pixels(c);
                const std::int32_t x1 = std::min<std::int32_t>(b.x + b.width, static_cast<std::int32_t>(W));
                const std::int32_t y1 = std::min<std::int32_t>(b.y + b.height, static_cast<std::int32_t>(H));
                b.x = std::max(b.x, 0);
                b.y = std::max(b.y, 0);
                b.width = x1 - b.x;
                b.height = y1 - b.y;
                if (!b.empty()) {
                    auto backdrop = g.pooled(W, H, rhi::Format::rgba8_unorm, rhi::TextureUsage::sampled, "render.backdrop");
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
                               c.mask,
                               {c.x0, c.y0, c.x1, c.y1}};
                    g.passes.push_back(std::move(p));
                }
                draws_into(parent, W, H, rhi::LoadOp::load, {});
                return;
            }
            draws_into(parent, W, H, rhi::LoadOp::load, {});
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
            const bool drawable = info && info->gpu.pass != EffectPass::none &&
                                  (info->gpu.pass != EffectPass::pixel || (info->gpu.fragment && template_shaped(*info->gpu.fragment)));
            if (!drawable) {
                ++report.effects_skipped;
                return false;  // drawn without it
            }
            EffectStep step;
            step.gpu = info->gpu;
            std::vector<float> params(push.params.begin(), push.params.end());
            for (std::size_t k = params.size(); k < info->params.size(); ++k) params.push_back(info->params[k].value);
            const float scale = transforms.front().max_scale();
            const float reach = std::ceil(effects().reach(push.effect, params) * scale) + 1;
            const Rect layer = m().map(push.bounds);
            const Rect screen{-reach, -reach, static_cast<float>(here().w) + 2 * reach, static_cast<float>(here().h) + 2 * reach};
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
            const auto rgba = rhi::TextureUsage::color_target | rhi::TextureUsage::sampled;
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
                auto aux = g.pooled(w, h, rhi::Format::rgba8_unorm, rgba, "render.effect.backdrop");
                if (!check(aux)) return true;
                step.aux = *aux;
                if (step.frost > 0) {
                    auto scratch = g.pooled(w, h, rhi::Format::rgba8_unorm, rgba, "render.effect.frost");
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
                    auto aux = g.pooled(h, 1, rhi::Format::r32_float, rhi::TextureUsage::sampled, "render.effect.rows");
                    if (!check(aux)) return true;
                    step.aux = *aux;
                    g.list.upload(step.aux, rhi::Region{0, 0, h, 1}, std::as_bytes(std::span(values)));
                }
            }
            auto source = g.pooled(w, h, rhi::Format::rgba8_unorm, rgba, "render.effect.source");
            auto result = g.pooled(w, h, rhi::Format::rgba8_unorm,
                                   info->gpu.pass == EffectPass::pixelate ? rhi::TextureUsage::compute_write | rhi::TextureUsage::sampled : rgba,
                                   "render.effect.result");
            if (!check(source) || !check(result)) return true;
            step.source = *source;
            step.result = *result;
            if (info->gpu.pass == EffectPass::blur) {
                auto scratch = g.pooled(w, h, rhi::Format::rgba8_unorm, rgba, "render.effect.scratch");
                if (!check(scratch)) return true;
                step.scratch = *scratch;
            }
            open.push_back({current, step.source, push.opacity, push.blend, true, step, {x0, y0, static_cast<float>(w), static_cast<float>(h)}});
            draws_into(step.source, w, h, rhi::LoadOp::clear, {0, 0, 0, 0});
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
            const rhi::Texture parent = g.passes[o.parent].target;
            const std::uint32_t W = g.passes[o.parent].w, H = g.passes[o.parent].h;
            draws_into(parent, W, H, rhi::LoadOp::load, {});
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

        const auto& all = dl.commands();
        for (std::size_t index = 0; index < all.size() && !failed; ++index) {
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
                        shape(c.rect, c.radii, c.paint);
                    } else if constexpr (std::same_as<T, StrokeRoundRect>) {
                        // Analytic, as a fill: the stroke's outer rrect less its inner (ui.slang's rring).
                        if (!(c.width > 0)) return;
                        const Ring ring = stroke_ring(c.rect, c.radii, c.width);
                        Draw d;
                        Prim& p = d.prim;
                        const Affine& mm = m();
                        int flags = (mm.b == 0 && mm.c == 0 && mm.a != 0 && mm.d != 0) ? flag_axis : 0;
                        if (!paint_into(p, d, flags, c.paint)) return;
                        clip_into(p, d, flags);
                        transform(p, mm, kind_rring, flags);
                        set(p.rect, ring.outer.x, ring.outer.y, ring.outer.width, ring.outer.height);
                        const CornerRadii& o = ring.outer_radii;
                        set(p.radii, o.top_left, o.top_right, o.bottom_right, o.bottom_left);
                        p.shape[0] = c.width;
                        emit(std::move(d));
                    } else if constexpr (std::same_as<T, BoxShadow>) {
                        const auto key = device_key(2).rect(c.rect).radii(c.radii).f32(c.offset.x).f32(c.offset.y).f32(c.blur).f32(c.spread).u64(c.inset).value();
                        mask(g.cover(key,
                                     [&](raster::Coverage& cov) {
                                         raster::box_shadow_coverage(c.rect, c.radii, c.offset, c.blur, c.spread, c.inset, m(), pixels(clips.back()), cov,
                                                                     g.polygons, g.rasterizer);
                                     }),
                             0, 0, Paint::solid(c.color));
                    } else if constexpr (std::same_as<T, FillPath>) {
                        const auto key = device_key(3).path(c.path).u64(static_cast<std::uint64_t>(c.rule)).value();
                        mask(g.cover(key,
                                     [&](raster::Coverage& cov) {
                                         raster::fill_coverage(paths.data(c.path), m(), static_cast<raster::FillRule>(c.rule), pixels(clips.back()), cov);
                                     }),
                             0, 0, c.paint);
                    } else if constexpr (std::same_as<T, StrokePath>) {
                        Key k = device_key(4).path(c.path).f32(c.style.width).u64(static_cast<std::uint64_t>(c.style.cap));
                        k.u64(static_cast<std::uint64_t>(c.style.join)).f32(c.style.miter_limit).f32(c.style.dash_offset).bytes(std::as_bytes(std::span(c.style.dashes)));
                        mask(g.cover(k.value(),
                                     [&](raster::Coverage& cov) {
                                         raster::stroke_coverage(paths.data(c.path),
                                                                 {.width = c.style.width,
                                                                  .cap = static_cast<raster::Cap>(c.style.cap),
                                                                  .join = static_cast<raster::Join>(c.style.join),
                                                                  .miter_limit = c.style.miter_limit,
                                                                  .dashes = c.style.dashes,
                                                                  .dash_offset = c.style.dash_offset},
                                                                 m(), pixels(clips.back()), cov);
                                     }),
                             0, 0, c.paint);
                    } else if constexpr (std::same_as<T, DrawImage>) {
                        const raster::Surface* img = res.images ? res.images->get(c.image) : nullptr;
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
                                const raster::PathData outline = res.glyphs->outline(c.font, gl.id);
                                if (!outline.verbs.empty()) {
                                    Key k;
                                    k.u64(6).u64(c.font.id).u64(gl.id).affine(pl.outline).pixels(pixels(clips.back()));
                                    mask(g.cover(k.value(),
                                                 [&](raster::Coverage& cov) {
                                                     raster::fill_coverage(outline, pl.outline, raster::FillRule::nonzero, pixels(clips.back()), cov);
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
                                             below.mask, below.coverage, below.key});
                        } else {
                            Key k;
                            k.u64(7).affine(mm).rect(c.rect).radii(c.radii);
                            push_mask_clip(k, [&](raster::Coverage& cov) {
                                raster::PathBuilder pb;
                                raster::rounded_rect_path(pb, c.rect, c.radii);
                                raster::fill_coverage(pb.data(), mm, raster::FillRule::nonzero, whole(), cov);
                            });
                        }
                    } else if constexpr (std::same_as<T, PushClipPath>) {
                        Key k;
                        k.u64(8).affine(m()).path(c.path).u64(static_cast<std::uint64_t>(c.rule));
                        push_mask_clip(k, [&](raster::Coverage& cov) {
                            raster::fill_coverage(paths.data(c.path), m(), static_cast<raster::FillRule>(c.rule), whole(), cov);
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
                        const std::optional<rhi::Texture> t = options.external ? options.external(c.texture) : std::nullopt;
                        const auto desc = t ? g.d.describe(*t) : std::nullopt;
                        if (!desc || !rhi::has(desc->usage, rhi::TextureUsage::sampled)) {
                            ++report.external_skipped;
                            return;
                        }
                        const auto w = static_cast<float>(desc->width), h = static_cast<float>(desc->height);
                        texture_image(*t, desc->width, desc->height, {0, 0, w, h}, c.dest, Sampling::linear, c.opacity, m());
                    }
                },
                all[index]);
        }
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
        if (!failed && g.atlas_overflow) fail({rhi::Errc::out_of_memory, "the frame's coverage does not fit the atlas"});
    }
    if (!failed && g.too_wide)
        fail({rhi::Errc::out_of_memory, std::format("a coverage mask {}×{} wider than the atlas", g.too_wide->width, g.too_wide->height)});
    g.too_wide.reset();
    if (failed) return std::unexpected(failure);

    // ── what the passes read: ramps, atlas, instances ────────────────────
    if (!g.ramp_rows.empty()) {
        if (auto r = g.ensure_ramps(static_cast<std::uint32_t>(g.ramp_rows.size())); !r) return std::unexpected(r.error());
        for (std::size_t i = 0; i < g.ramp_rows.size(); ++i)
            g.list.upload(g.ramps, rhi::Region{0, static_cast<std::uint32_t>(i), 256, 1}, std::as_bytes(std::span(g.ramp_rows[i])));
    } else if (auto r = g.ensure_ramps(1); !r) {
        return std::unexpected(r.error());
    }
    std::size_t total = 0;
    for (const Pass& p : g.passes) total += p.draws.size();
    if (total > 0) {
        const auto bytes = static_cast<std::uint32_t>(total * sizeof(Prim));
        if (bytes > g.instance_capacity) {
            if (g.instances) g.d.destroy(g.instances);
            std::uint32_t cap = std::max<std::uint32_t>(64 * sizeof(Prim), g.instance_capacity);
            while (cap < bytes) cap *= 2;
            auto b = g.d.create(rhi::BufferDesc{cap, rhi::BufferUsage::vertex, "render.instances"});
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
    std::vector<rhi::Texture> textures;
    auto add = [&](rhi::Texture t) {
        if (t && std::ranges::find(textures, t) == textures.end()) textures.push_back(t);
    };
    std::uint32_t first = 0;
    for (const Pass& s : g.passes) {
        const std::uint32_t base = first;
        first += static_cast<std::uint32_t>(s.draws.size());
        if (s.kind == Pass::Kind::effect) {
            const EffectStep& e = s.effect;
            render::PassDesc desc{.name = "render.vector.effect"};
            desc.reads = {e.source};
            if (e.backdrop && e.parent) desc.reads.push_back(e.parent);
            for (rhi::Texture t : {e.result, e.aux, e.scratch})
                if (t) desc.writes.push_back(t);
            g.graph.pass(std::move(desc), [&g, &s](rhi::CommandList&) { g.record_effect(s.effect); });
            continue;
        }
        if (s.kind == Pass::Kind::blend) {
            const BlendPass& b = s.blend;
            render::PassDesc desc{.name = "render.vector.blend", .reads = {s.target, b.layer}, .writes = {s.target, b.backdrop}};
            if (b.mask) desc.reads.push_back(b.mask);
            g.graph.pass(std::move(desc), [&g, &s](rhi::CommandList& list) {
                const BlendPass& b = s.blend;
                const auto W = static_cast<float>(s.w), H = static_cast<float>(s.h);
                list.copy(s.target, b.box, b.backdrop, b.box.x, b.box.y);
                list.begin_render({s.target, rhi::LoadOp::load, {}});
                const BlendParams params{{W, H, 0, 0},
                                         {static_cast<float>(b.box.x), static_cast<float>(b.box.y), static_cast<float>(b.box.x + b.box.width),
                                          static_cast<float>(b.box.y + b.box.height)},
                                         {static_cast<float>(b.mode), static_cast<float>(b.opacity), b.mask ? 1.0f : 0.0f, 0},
                                         b.clip};
                list.set_pipeline(g.blend_pipeline);
                list.push_uniforms(rhi::Stage::vertex, 0, params);
                list.push_uniforms(rhi::Stage::fragment, 0, params);
                list.bind_texture(rhi::Stage::fragment, 0, b.layer, g.nearest);
                list.bind_texture(rhi::Stage::fragment, 1, b.backdrop, g.nearest);
                list.bind_texture(rhi::Stage::fragment, 2, b.mask ? b.mask : g.white_mask, g.nearest);
                list.draw(6);
                list.end_render();
            });
            continue;
        }
        // A pass that only loads and draws nothing does nothing.
        if (s.draws.empty() && s.load == rhi::LoadOp::load) continue;
        textures.clear();
        if (s.load == rhi::LoadOp::load) add(s.target);
        for (const Draw& d : s.draws) {
            add(d.image);
            add(d.mask);
        }
        render::PassDesc desc{.name = "render.vector.draws", .reads = textures, .writes = {s.target}};
        g.graph.pass(std::move(desc), [&g, &s, base](rhi::CommandList& list) {
            list.begin_render({s.target, s.load, s.clear});
            if (!s.draws.empty()) {
                const Frame frame{{static_cast<float>(s.w), static_cast<float>(s.h), static_cast<float>(g.atlas_w), static_cast<float>(g.atlas_h)},
                                  {static_cast<float>(g.ramp_capacity), 0, 0, 0}};
                list.set_pipeline(g.pipeline);
                list.push_uniforms(rhi::Stage::vertex, 0, frame);
                list.push_uniforms(rhi::Stage::fragment, 0, frame);
                list.bind_vertex_buffer(0, g.instances);
                list.bind_texture(rhi::Stage::fragment, 0, g.ramps, g.nearest);
                list.bind_texture(rhi::Stage::fragment, 1, g.atlas, g.nearest);
                std::size_t i = 0;
                while (i < s.draws.size()) {
                    const Draw& head = s.draws[i];
                    std::size_t j = i + 1;
                    while (j < s.draws.size() && s.draws[j].image == head.image && s.draws[j].mask == head.mask) ++j;
                    list.bind_texture(rhi::Stage::fragment, 2, head.image ? head.image : g.white, g.nearest);
                    list.bind_texture(rhi::Stage::fragment, 3, head.mask ? head.mask : g.white_mask, g.nearest);
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
        for (const render::GraphProblem& p : recorded.error()) why += (why.empty() ? "" : "; ") + p.text;
        return rhi::fail(rhi::Errc::invalid, "render: the frame's passes: " + why);
    }
    report.gpu_passes = recorded->recorded;
    report.gpu_passes_culled = recorded->culled;
    for (const rhi::Command& c : g.list.commands()) {
        if (const auto* u = std::get_if<rhi::cmd::UploadTexture>(&c)) report.uploaded += u->data.size();
        else if (const auto* b = std::get_if<rhi::cmd::UploadBuffer>(&c)) report.uploaded += b->data.size();
    }
    auto submitted = g.d.submit(g.list);
    for (rhi::Texture t : g.retired) g.d.destroy(t);  // safe once submitted: released when the GPU is done
    g.retired.clear();
    if (!submitted) return std::unexpected(submitted.error());
    report.rows = g.height;
    report.masks_made = g.masks_made;
    report.masks_reused = g.masks_reused;
    return report;
}

}  // namespace omui::render
