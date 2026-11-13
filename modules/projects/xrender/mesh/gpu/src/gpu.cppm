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
export import :mesh_shaders;
export import :ids_shaders;
export import :sky_shaders;
export import :shadow_shaders;
export import :glow_shaders;

export namespace xrender::mesh::gpu {

inline constexpr std::size_t most_lamps = 8;

struct Stats {
    std::uint32_t draws = 0, missing = 0;  // drawn; naming no mesh loaded or no material
    std::uint64_t triangles = 0;
    std::uint32_t lamps = 0, lamps_left_out = 0;  // lit by; past `most_lamps`
    std::uint32_t casters = 0;                    // drawn into the sun's map
    std::uint32_t lamps_shadowed = 0, lamp_views = 0;  // lamps and spots with a row of the lamps' map; views drawn for them
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
    [[nodiscard]] const Stats& last() const noexcept { return last_; }
    // The formats the pass's shader was compiled to by this build: what a device is opened with.
    [[nodiscard]] static mlibs::rhi::ShaderFormats shader_formats() noexcept;

private:
    Pass() = default;
    void release() noexcept;
    struct Loaded {
        mlibs::rhi::Buffer corners;
        std::uint32_t      count = 0;
    };
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline pipeline_, ids_pipeline_, sky_pipeline_, shadow_pipeline_, lamp_shadow_pipeline_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Texture          no_shadow_;  // a texel, for the map's place when there is no map
    mlibs::rhi::Buffer           cover_;  // three corners over the picture: what the sky is drawn with
    std::vector<Loaded>          meshes_;
    bool                         reversed_ = false, light_ = false;
    Stats                        last_;
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
                const Bloom& bloom);

private:
    Glow() = default;
    void release() noexcept;
    mlibs::rhi::Device*          device_ = nullptr;
    mlibs::rhi::GraphicsPipeline spread_, shown_;
    mlibs::rhi::Sampler          nearest_;
    mlibs::rhi::Buffer           cover_;
};

// The draw a pixel of an ids picture names (`pixels`: what the device read back of it, four bytes a pixel, the
// least first): its place in the frame's draws, or `none`.
[[nodiscard]] std::uint32_t who(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) noexcept;

}  // namespace xrender::mesh::gpu
