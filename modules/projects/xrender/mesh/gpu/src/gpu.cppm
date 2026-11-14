// xrender.mesh.gpu — the mesh pass on a device (the foundation plan's §5.1–§5.3, §5.7: the CPU is the
// reference, the GPU the same contract's other path).
//
//     auto pass = *xrender::mesh::gpu::Pass::make(device);
//     pass.load(assets.meshes);                                        // each mesh's corners, once
//     mlibs::rhi::CommandList list;
//     pass.record(list, scene, extracted, view, assets.materials, colour, depth, options);
//     device.submit(list);                                             // `colour`: rgba8, sRGB, as the reference's
//
// It takes what the reference takes — the scene, what `xrender.scene` extracted for the view, the materials,
// the options — and depends on `mlibs.rhi`'s interface alone: the device is whoever made it (SDL_GPU, WebGPU).
// What it draws is held to the reference by the device suites (a backend's tests), within a tolerance said
// there. What it does not do yet is said in the README.
export module xrender.mesh.gpu;

import std;
export import xrender.mesh;
export import xrender.scene;
export import mlibs.rhi;
import mlibs.math;
export import :mesh_shaders;
export import :ids_shaders;
export import :sky_shaders;
export import :shadow_shaders;
export import :glow_shaders;
export import :velocity_shaders;
export import :resolve_shaders;
export import :open_shaders;

export namespace xrender::mesh::gpu {

inline constexpr std::size_t most_lamps = 8;

struct Stats {
    std::uint32_t draws = 0, missing = 0;  // drawn; naming no mesh loaded or no material
    std::uint64_t triangles = 0;
    std::uint32_t lamps = 0, lamps_left_out = 0;  // lit by; past `most_lamps`
    std::uint32_t casters = 0;                    // drawn into the sun's map
    std::uint32_t textured = 0;                   // draws whose colour is looked up in a picture
    std::uint32_t lamps_shadowed = 0, lamp_views = 0;  // lamps and spots with a row of the lamps' map; views drawn for them
    // The colour pass's clusters: looked at, and left out — outside the view, or every triangle facing away
    // (`Options::cull_clusters`; no pixel changes) — and the draw calls their runs took.
    std::uint32_t clusters = 0, clusters_outside = 0, clusters_away = 0, runs = 0;
    // The shadow views' clusters (the sun's map and each lamp's views, summed): looked at, left out as outside
    // the view the light sees by, and the draw calls. What faces away from a light is drawn: a map holds both
    // sides of what casts, so only what the view cannot hold is left out, and no texel of a map changes.
    std::uint32_t shadow_clusters = 0, shadow_clusters_outside = 0, shadow_runs = 0;
    std::uint64_t shadow_triangles = 0;
};

// The sun's shadows by a map (the reference traces a ray; a device looks up what the sun saw): two textures of
// `size` × `size` the caller made — `map` r32_float (a colour target, sampled), `depth` d32_float (a depth
// target) — and how far from the eye things are shadowed. Everything of the scene that casts is drawn into it,
// seen by the view or not. Of the first sun only.
//
// Lamps' and spots' shadows are a pair of their own, `lamps_map` (r32_float) and `lamps_depth` (d32_float), each
// 6 × `lamp_size` wide and `lamps` × `lamp_size` high: six views about each of the frame's first `lamps` lamps
// and spots (a spot's only where its cone looks), each texel how far the nearest thing is. Either pair may be
// left out: those lights then shine through things.
struct Shadows {
    mlibs::rhi::Texture map, depth;
    std::uint32_t       size = 2048;
    float               distance = 40;  // from the eye: what the map covers
    float               behind = 200;   // toward the sun from there: how far off a caster may stand
    mlibs::rhi::Texture lamps_map, lamps_depth;
    std::uint32_t       lamp_size = 512, lamps = 4;
    // What stands in the way of the light from everywhere: `Open`'s picture of this frame (r32_float, the
    // target's size). Left out: every place is wholly open.
    mlibs::rhi::Texture open;
};

class Pass {
public:
    // The pipeline: the pass's shader, a depth test (`reversed`: the nearest at 1), an rgba8 target. `light`: the
    // target is rgba16_float instead and holds the light before the display's curve — what `Glow` takes.
    [[nodiscard]] static mlibs::rhi::Result<Pass> make(mlibs::rhi::Device& device, bool reversed_depth = false, bool light = false);
    Pass(Pass&& other) noexcept;
    Pass& operator=(Pass&& other) noexcept;
    ~Pass();

    // Each mesh's corners as the device takes them (a triangle's three, with the mesh's normals or the face's),
    // uploaded and waited for. A mesh of no triangles has no buffer and is not drawn. Called again it replaces
    // what was loaded.
    [[nodiscard]] mlibs::rhi::Result<void> load(std::span<const mlibs::geom::ClusteredMesh* const> meshes);
    // The pictures materials name (`Material::texture`), as the device takes them: each one's sizes laid on one
    // sheet of light (rgba16_float), which the shader reads as the reference reads its own — between four texels,
    // between two sizes. Called again it replaces what was loaded. A mesh with no texture coordinates is plain.
    [[nodiscard]] mlibs::rhi::Result<void> load_textures(std::span<const Texture* const> textures);
    // The frame, recorded: one pass on `colour` (rgba8_unorm, or rgba16_float for a pass of light; a colour target) and `depth` (d32_float, a depth
    // target) of `width` × `height`, cleared to the background; where there is fog the background is drawn
    // through it first, each pixel along its own ray — the sky that clears upward, as the reference has it.
    void record(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, std::span<const Material> materials,
                mlibs::rhi::Texture colour, mlibs::rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options = {}, const Shadows* shadows = nullptr);
    // Who drew each pixel, recorded: a pass of its own on `ids` (r32_uint, a colour target) and `depth` — for
    // every pixel the place of its draw in `extracted.draws` and one more; 0 where nothing is. What an agent
    // asks of a pixel of the device's picture (`who`), with nothing of the CPU's drawn.
    void record_ids(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture ids, mlibs::rhi::Texture depth,
                    std::uint32_t width, std::uint32_t height);
    // Which thing is at each pixel, for the resolve over time: a pass on `who` (r32_float, a colour target that
    // can be sampled) — the instance's own id (its low 24 bits, and one more; 0 where nothing is), which is the
    // same from frame to frame where a draw's place is not. Sampled where `options.jitter` says.
    void record_who(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture who, mlibs::rhi::Texture depth,
                    std::uint32_t width, std::uint32_t height, const Options& options = {});
    // How far each pixel's point of the surface moved on the screen since the last frame recorded here: a pass
    // on `velocity` (rgba16_float: x right, y down, in pixels) — and the pass remembers where everything is, for
    // the next. What stood still under a still view says exactly 0; the jitter is not a movement.
    void record_velocity(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture velocity,
                         mlibs::rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options = {});
    // Both of them — `record_velocity`'s picture and `record_who`'s — in one drawing of the scene where the
    // device has two colour targets a pass (Caps::max_color_targets), and as the two passes where it has one:
    // the same pictures either way.
    void record_velocity_and_who(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture velocity,
                                 mlibs::rhi::Texture who, mlibs::rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options = {});
    // The surface at each pixel, for `Open`: a pass on `surface` (rgba16_float, a colour target that can be
    // sampled) — which way it faces and how far from the eye it is (to 250; further is as nothing there).
    void record_surface(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture surface,
                        mlibs::rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options = {});
    // After a cut: nothing is remembered, and the next frame's velocity is 0.
    void forget() noexcept { remembered_ = false; }
    [[nodiscard]] const Stats& last() const noexcept { return last_; }
    // The formats the pass's shader was compiled to by this build: what a device is opened with.
    [[nodiscard]] static mlibs::rhi::ShaderFormats shader_formats() noexcept;

private:
    Pass() = default;
    void release() noexcept;
    void velocity_pass(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture velocity,
                       mlibs::rhi::Texture who, mlibs::rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options);
    void who_pass(mlibs::rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, mlibs::rhi::Texture target, mlibs::rhi::Texture depth,
                  std::uint32_t width, std::uint32_t height, mlibs::rhi::GraphicsPipeline pipeline, bool by_instance, mlibs::math::Vec2 jitter);
    struct Loaded {
        mlibs::rhi::Buffer corners;
        std::uint32_t      count = 0;
        bool               placed = false;  // it has texture coordinates
        std::vector<mlibs::geom::Cluster> clusters;  // each a run of the corners: three a triangle from 3 × its first triangle
    };
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline pipeline_, ids_pipeline_, sky_pipeline_, shadow_pipeline_, lamp_shadow_pipeline_, who_pipeline_, velocity_pipeline_, velocity_who_pipeline_, surface_pipeline_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Texture          no_shadow_;  // a texel, for the map's place when there is no map
    mlibs::rhi::Buffer           cover_;  // three corners over the picture: what the sky is drawn with
    std::vector<Loaded>          meshes_;
    struct Sheet {
        mlibs::rhi::Texture texture;
        float               width = 0, height = 0, sizes = 0, sheet_width = 1, sheet_height = 1;
    };
    std::vector<Sheet>           textures_;
    bool                         reversed_ = false, light_ = false;
    std::vector<Id>              before_ids_;    // the scene's instances a frame ago, in id order
    std::vector<mlibs::math::Mat4> before_world_;
    mlibs::math::Mat4            before_clip_;
    bool                         remembered_ = false;
    Stats                        last_;
    struct Run {
        std::uint32_t first = 0, count = 0;  // corners
    };
    std::vector<Run>             runs_;  // a draw's, kept between draws
    bool shadow_runs_for(const Loaded& loaded, const mlibs::math::Mat4& world, const mlibs::geom::Frustum& sees, bool cull);
    void runs_for(const Loaded& loaded, const mlibs::math::Mat4& world, const std::optional<mlibs::math::Mat4>& back, const mlibs::geom::Frustum& frustum, mlibs::math::Vec3 eye,
                  bool cull, bool counted);
};

// The frame as shown, on a device (the reference: `xrender::mesh::Glow`): the light a pass of light drew, what of
// it is brighter than the display spread about it, the display's curve. Six passes between two pictures of the
// caller's and one onto `colour`.
class Glow {
public:
    [[nodiscard]] static mlibs::rhi::Result<Glow> make(mlibs::rhi::Device& device);
    Glow(Glow&& other) noexcept;
    Glow& operator=(Glow&& other) noexcept;
    ~Glow();
    // `light`, `one`, `other`: rgba16_float, colour targets that can be sampled, `width` × `height` (`one` and
    // `other` are working space); `colour`: rgba8_unorm, a colour target. With no radius: the light through the curve.
    void record(mlibs::rhi::CommandList& list, mlibs::rhi::Texture light, mlibs::rhi::Texture one, mlibs::rhi::Texture other, mlibs::rhi::Texture colour, std::uint32_t width, std::uint32_t height,
                const Bloom& bloom, const Tone& tone = {});

private:
    Glow() = default;
    void release() noexcept;
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline spread_, shown_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Buffer           cover_;
};

// Frames gathered over time, on a device (the reference: `xrender::mesh::Temporal`): each frame drawn with
// `Options::jitter = jitter_of(n)`, its light, `record_velocity`'s and `record_who`'s pictures, and the last
// frame's result and who — kept from frame to frame (the render graph's `history`).
class Temporal {
public:
    [[nodiscard]] static mlibs::rhi::Result<Temporal> make(mlibs::rhi::Device& device);
    Temporal(Temporal&& other) noexcept;
    Temporal& operator=(Temporal&& other) noexcept;
    ~Temporal();
    // One pass onto `gathered` (rgba16_float: the light, and in its fourth part how many frames are gathered at
    // the pixel). `begun`: there is a last result — `gathered_before` and `who_before` hold the last frame's
    // (a history's `past` of 1); without it they are not read and the result is the frame's own.
    void record(mlibs::rhi::CommandList& list, mlibs::rhi::Texture light, mlibs::rhi::Texture velocity, mlibs::rhi::Texture who, mlibs::rhi::Texture who_before, mlibs::rhi::Texture gathered_before,
                mlibs::rhi::Texture gathered, std::uint32_t width, std::uint32_t height, bool begun, float least = 0.06f);

private:
    Temporal() = default;
    void release() noexcept;
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline pipeline_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Buffer           cover_;
};

// How open each pixel's place is to the light from everywhere, on a device (the reference: `Options::
// occlusion_rays`, rays through the scene): rays about the surface's normal stepped through the picture
// `Pass::record_surface` drew. It knows what the eye sees and no more: what is behind a thing or off the
// picture shuts nothing.
class Open {
public:
    [[nodiscard]] static mlibs::rhi::Result<Open> make(mlibs::rhi::Device& device);
    Open(Open&& other) noexcept;
    Open& operator=(Open&& other) noexcept;
    ~Open();
    // One pass onto `open` (r32_float, a colour target that can be sampled; hand it to the mesh pass as
    // `Shadows::open`): `options.occlusion_rays` rays a pixel (32 at the most) of `steps` steps each, as far as
    // `options.occlusion_reach`; the same picture each time for a seed.
    void record(mlibs::rhi::CommandList& list, const scene::View& view, mlibs::rhi::Texture surface, mlibs::rhi::Texture open, std::uint32_t width, std::uint32_t height,
                const Options& options, std::uint32_t steps = 12);

private:
    Open() = default;
    void release() noexcept;
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline pipeline_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Buffer           cover_;
};

// The draw a pixel of an ids picture names (`pixels`: what the device read back of it, four bytes a pixel, the
// least first): its place in the frame's draws, or `none`.
[[nodiscard]] std::uint32_t who(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) noexcept;

}  // namespace xrender::mesh::gpu
