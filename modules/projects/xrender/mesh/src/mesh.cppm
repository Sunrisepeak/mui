// xrender.mesh — meshes drawn: the CPU reference (the foundation plan's §5.1–§5.3, M6.3; D-164, D-184).
//
//     mesh::Pass pass;
//     mesh::Targets frame;
//     frame.resize(640, 360);
//     scene::extract(world, view, seen);
//     pass.draw(world, seen, view, {.meshes = meshes, .materials = materials}, frame);
//     const auto who = mesh::at(frame, 320, 180);   // the instance, cluster and triangle at a pixel
//
// One pass over what the scene extracted writes, for each pixel, the colour, the depth, the surface's normal,
// how far it moved since the last frame, and who drew it. Triangles are clipped to the view, their corners snapped
// to 1/256 of a pixel and filled by the top-left rule, so two triangles that share an edge share no pixel and
// leave none between them; every number is IEEE arithmetic in a fixed order — the same bits on every machine.
// It is the reference a GPU path of the same contract is compared with, and what an agent or a test with no GPU
// looks at.
export module xrender.mesh;

import std;
export import xrender.scene;

export namespace xrender::mesh {

using mlibs::math::Mat4;
using mlibs::math::Vec2;
using mlibs::math::Vec3;
using xrender::scene::Id;

// A surface, as data. The reference's shading is the least that shows a form: what a light gives a matt surface,
// and what it gives off itself. (The full model — roughness, metal, textures — is the material interface's,
// with the GPU path.)
// A surface. `roughness` 1 and `metallic` 0 (what a material says when it says neither) is matt: light from a
// lamp by how squarely it falls, nothing mirrored. Smoother, or metal: a highlight as well — the microfacet
// model every engine draws with (GGX's spread, Smith's shadowing as Schlick wrote it, Schlick's Fresnel), from
// +, ×, ÷ and a square root alone, so its bits are the same everywhere. A metal has no colour of its own but
// its highlight's.
struct Material {
    Vec3  base{0.8f, 0.8f, 0.8f};  // linear
    Vec3  emissive;
    float roughness = 1;  // 1 matt … 0 a mirror (held to 0.04 at least: a point of light has no size)
    float metallic = 0;   // 0 … 1
    std::uint32_t texture = 0xffffffffu;  // none, or which of `Assets::textures` its base colour is multiplied by
    [[nodiscard]] constexpr bool matt() const noexcept { return roughness >= 1 && metallic <= 0; }
};
// What a surface sends toward the eye of light arriving along `toward_light` (all of length 1; `facing` the
// normal's part of it, above 0), as a share of that light a channel: what `Pass` and a device's shader both do.
[[nodiscard]] Vec3 reflected(const Material& material, Vec3 normal, Vec3 toward_eye, Vec3 toward_light, float facing) noexcept;
// And of the light from everywhere.
[[nodiscard]] Vec3 reflected_ambient(const Material& material) noexcept;
// What the scene's numbers name: a draw's `mesh` and `material` are places here. One that names nothing is not
// drawn, and counted.
// A picture laid over a surface by its mesh's texture coordinates (`mlibs.geom`'s `Mesh::uvs`; it repeats). It is
// kept as light — the display's curve taken off once, by a table — at every size down to one texel, each the mean
// of four of the one before: a surface far away or seen at a slant takes its colour from the size whose texels
// are about a pixel across, mixed with the next, so a pattern does not shimmer or band where it is small.
struct Texture {
    int width = 0, height = 0;
    std::vector<Vec3>        light;   // every size's texels, a row after a row, the finest size first
    std::vector<std::uint32_t> sizes; // where each size begins in `light`
    // From rgba8 in sRGB (`width` × `height` × 4, rows from the top). Nothing: no size, no pixels, or too few bytes.
    [[nodiscard]] static std::optional<Texture> made(std::span<const std::uint8_t> rgba, int width, int height);
    // Its light at `uv` (0 … 1 across and down, repeating), for a pixel that covers `footprint` texels of the finest
    // size across. Between four texels of a size, between two sizes. The same bits everywhere.
    [[nodiscard]] Vec3 at(Vec2 uv, float footprint) const noexcept;
    [[nodiscard]] std::size_t size_count() const noexcept { return sizes.size(); }
};

struct Assets {
    std::span<const mlibs::geom::ClusteredMesh* const> meshes;
    std::span<const Material> materials;
    // None, or a tree of each mesh's triangles (mlibs.geom's `bvh_of`), a mesh each: with them a light is hidden
    // where something stands between — a ray from the surface to the light, through the trees (D-184: the ray
    // query is geometry's). A mesh with no tree casts no shadow.
    std::span<const mlibs::geom::Bvh* const> trees;
    std::span<const Texture* const> textures;  // what `Material::texture` names
};
// Fog: what is between the eye and a surface, taken as a share of the surface's light and given the fog's own.
// Its density is `density` a unit of length at the height `base` and thinner by e each 1/`falloff` above it
// (0: the same at every height); how much is gone through along a ray is worked out exactly, not stepped. The
// background is a surface infinitely far: looking up through thinning fog it shows, looking level it does not.
struct Fog {
    Vec3  colour{0.5f, 0.6f, 0.7f};
    float density = 0;  // 0: no fog — the pass does nothing for it
    float base = 0, falloff = 0;
};
// The share of a surface's light that reaches the eye through `fog`, along a ray from `eye` toward `toward` (of
// length 1) for `length` (which may be infinite). Deterministic (`mlibs.math`'s exp): the same bits everywhere.
[[nodiscard]] float through(const Fog& fog, Vec3 eye, Vec3 toward, float length) noexcept;

// What is about the scene, for a surface with a highlight to mirror: a sky above, the land at eye level, the
// ground below — three colours, and a way looked along is between two of them by how far up or down it goes.
// The least an environment can be; a picture of one (a map) is the same question asked of a texture. A matt
// surface does not ask it. Off: nothing is mirrored but the lights.
struct Environment {
    bool on = false;
    Vec3 above{0.35f, 0.5f, 0.8f}, level{0.6f, 0.6f, 0.6f}, below{0.15f, 0.13f, 0.1f};
    [[nodiscard]] constexpr Vec3 toward(Vec3 way) const noexcept {  // `way` of length 1
        const float up = way.y > 0 ? way.y : -way.y;
        const Vec3 far = way.y > 0 ? above : below;
        return level + (far - level) * up;
    }
};
// What a surface mirrors of the environment toward the eye: what lies along the eye's way turned about the
// normal, less sharply the rougher it is (toward the level's colour), by how much the surface mirrors there.
[[nodiscard]] Vec3 mirrored(const Material& material, const Environment& environment, Vec3 normal, Vec3 toward_eye) noexcept;

struct Options {
    Vec3 background{0.02f, 0.02f, 0.03f};
    // Light from everywhere: the one implementation there is of the indirect light (a constant), behind which
    // baked light and probes come.
    Vec3 ambient{0.06f, 0.06f, 0.07f};
    bool reversed_depth = false;  // the projection puts the nearest at 1 (math::perspective_reversed)
    bool cull_clusters = true;    // leave out a cluster outside the view or wholly facing away: no pixel changes
    bool shadows = true;          // where the assets have trees: a light reaches a surface only if nothing is between
    // Every sample of this frame taken this far from its pixel's middle (pixels: x right, y down; within half a
    // pixel). A different one each frame, and the frames gathered over time (`Temporal`), is anti-aliasing. The
    // velocity does not include it.
    Vec2 jitter{0, 0};
    Fog  fog;
    Environment environment;
};

inline constexpr std::uint32_t none = 0xffffffffu;
// Who drew a pixel: the draw (its place in Targets::draws), and in its mesh the cluster and the triangle.
struct Pixel {
    std::uint32_t draw = none, cluster = none, triangle = none;
};
struct Targets {
    int width = 0, height = 0;
    std::vector<std::uint8_t> colour;    // rgba8, sRGB, a row after a row from the top
    std::vector<Vec3>         light;     // the same before the display's curve (linear, not held to 1): what is gathered over time
    std::vector<float>        depth;     // as the projection gives it (0 … 1)
    std::vector<Vec3>         normal;    // in the world, of length 1; nothing drawn: 0
    std::vector<Vec2>         velocity;  // pixels moved since the last frame drawn (x right, y down)
    std::vector<Pixel>        ids;
    std::vector<scene::Draw>  draws;     // the frame's, as extracted: what `Pixel::draw` names
    void resize(int new_width, int new_height);
};
struct Stats {
    std::uint32_t draws = 0, missing = 0;                   // drawn; naming no mesh or no material
    std::uint32_t clusters = 0, clusters_outside = 0, clusters_away = 0;
    std::uint64_t triangles = 0, facing_away = 0, outside = 0, clipped = 0, filled = 0;
    std::uint64_t written = 0, hidden = 0;                  // pixels that passed the depth, and that did not
    std::uint64_t rays = 0, shadowed = 0;                   // rays sent toward a light, and those that met something
    std::uint64_t textured = 0;                             // samples whose colour was looked up in a texture
    std::uint64_t fogged = 0;                               // samples whose light went through fog: each one written, and the background's pixels
};

// What can stand between a surface and a light, as a frame holds it: an instance's mesh and its tree, the world
// taken to the mesh, its box in the world.
struct Caster {
    Mat4                     back;
    mlibs::geom::Box3        bounds;
    const mlibs::geom::Mesh* mesh = nullptr;
    const mlibs::geom::Bvh*  tree = nullptr;
};

class Pass {
public:
    // The frame. `extracted` is of `scene` and `view`; `out` has a size. Remembers where everything was for the
    // next frame's velocity.
    void draw(const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, const Assets& assets, Targets& out, const Options& options = {});
    [[nodiscard]] const Stats& last() const noexcept { return last_; }
    // The next frame's velocity is 0 everywhere: after a cut, or a jump of the camera.
    void forget() noexcept { remembered_ = false; }

private:
    Stats             last_;
    std::vector<Id>   before_ids_;     // the last frame's instances, in id order
    std::vector<Mat4> before_world_;
    Mat4              before_clip_;    // its world to clip
    std::vector<Caster> casters_;      // a frame's: every instance of the scene that can cast, seen or not
    bool              remembered_ = false;
};

// Where frame `number`'s samples are taken, for `Options::jitter`: a sequence that fills a pixel evenly (Halton's,
// bases 2 and 3), 16 long, each within half a pixel of the middle.
[[nodiscard]] Vec2 jitter_of(std::uint32_t number) noexcept;

// Frames gathered over time: anti-aliasing from frames whose samples were each taken a little aside (`jitter`).
// A pixel's light is the last result's where its surface was a frame ago (the velocity), mixed with the frame's own
// — unless neither there nor beside it was this instance (the ids): then the frame's own, and it starts
// again. What is carried is held to what the frame's pixels around it show, so what has changed does not linger.
// The reference of the method: a GPU's resolve is held to it.
class Temporal {
public:
    struct Told {
        std::uint32_t carried = 0, another = 0, outside = 0;  // pixels that kept their past; that were another thing's; whose past was off the frame
    };
    // `frame` as drawn (its light, velocity and ids); `colour` rgba8, sRGB, the frame's size.
    Told resolve(const Targets& frame, std::vector<std::uint8_t>& colour);
    [[nodiscard]] std::span<const Vec3> light() const noexcept { return light_; }
    void forget() noexcept { width_ = height_ = 0; }
    float least = 0.06f;  // the newest frame's share once a pixel has settled

private:
    int                        width_ = 0, height_ = 0;
    std::vector<Vec3>          light_, next_;
    std::vector<Id>            who_, next_who_;   // the instance at each pixel of the last result; 0: none
    std::vector<std::uint16_t> age_, next_age_;   // frames gathered there
};

// Who drew the pixel, back to the scene's instance — or nothing did.
struct Source {
    Id            instance = 0;
    std::uint32_t mesh = 0, material = 0, cluster = 0, triangle = 0;
    float         depth = 0;
    Vec3          normal;
};
[[nodiscard]] std::optional<Source> at(const Targets& targets, int x, int y) noexcept;

// For a frame's layer of this picture (xrender.frame's `PictureLayer`): who drew each pixel as one number — the draw's
// place and one, 0 where nothing drew — and what `frame.at` says of a pixel beside it, as JSON members
// (`"instance":11,"mesh":2,"material":2,"cluster":9,"triangle":640,"depth":0.93,"normal":[0,0.6,0.8]`).
void objects(const Targets& targets, std::vector<std::uint32_t>& out);
void describe(const Targets& targets, int x, int y, std::string& members);

// What is under a point of a view, found by a ray and nothing drawn: the nearest front of an instance's mesh along
// the ray through (`x`, `y`) of a frame `width` × `height` (pixels; a pixel's middle is at +0.5). Through the
// meshes' trees (`Assets::trees`; a mesh without one is passed through). Hidden instances and those on none of the
// view's layers are not there. The id buffer's answer for the pixel, without the frame: what a pointer is over,
// what an agent asks before anything is rendered. `Source::cluster` is not known to a ray: `none`.
[[nodiscard]] std::optional<Source> pick(const scene::Scene& scene, const scene::View& view, const Assets& assets, float x, float y, int width, int height);

// Light brighter than the display can show, spread to the pixels about it: what tells a lamp from a white wall.
// It is taken from the light before the display's curve (`Targets::light`, or `Temporal::light()`), which is
// where "brighter than white" still exists.
struct Bloom {
    float threshold = 1;     // of a pixel's brightest part: what is above this spreads
    float radius = 0;        // pixels, about a third of how far it reaches; below 1: none
    float strength = 0.5f;   // how much of what spread is added
};
// The frame as shown: the light, what of it spread, the display's curve. The spread is three box passes each
// way (close to a bell), in a fixed order: the same bits everywhere. What would spread from beyond the frame's
// edge is not there, and what spreads past it is lost. Keeps its working space between frames.
class Glow {
public:
    struct Told {
        std::uint32_t bright = 0;  // pixels with something to spread
    };
    // `colour` becomes rgba8, sRGB, `width` × `height`. With no radius it is the light through the curve.
    Told show(std::span<const Vec3> light, int width, int height, const Bloom& bloom, std::vector<std::uint8_t>& colour);
    [[nodiscard]] std::span<const Vec3> spread() const noexcept { return spread_; }  // the last frame's, before `strength`

private:
    std::vector<Vec3> spread_, other_;
};

// What of an instance reached the frame: how many pixels, and the box of them (x0, y0 inclusive; x1, y1 exclusive).
struct Shown {
    Id            instance = 0;
    std::uint32_t pixels = 0;
    int           x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// Every instance with a pixel, in id order. An instance that was a draw and is not here is behind something (or
// too small to cover a pixel's middle).
void shown(const Targets& targets, std::vector<Shown>& out);

}  // namespace xrender::mesh
