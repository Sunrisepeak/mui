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
    // The surface's other pictures (glTF's set): each none, or one of `Assets::textures`, laid by the same texture
    // coordinates as the base colour's. The first, the second and the fourth are numbers and not colours (`Coding::plain`).
    std::uint32_t surface = 0xffffffffu;  // its green multiplies `roughness`, its blue `metallic`
    std::uint32_t normal = 0xffffffffu;   // which way the surface faces about its own normal: red along the picture, green up it, blue out (each × 2 − 1)
    std::uint32_t glow = 0xffffffffu;     // a colour: multiplies `emissive`
    std::uint32_t shade = 0xffffffffu;    // its red is the share of the light from everywhere that reaches the surface (often the same picture as `surface`)
    float normal_scale = 1;    // the normal picture's red and green by this much
    float shade_strength = 1;  // 0: the shade picture says nothing … 1: all it says
    // How much of what is behind it the surface hides: 1 all of it. The base colour's picture may say so too, a
    // texel at a time (its fourth byte, `Texture::cover`), and the two multiply.
    float opacity = 1;
    // Above 0: where the surface hides less than this, it is not there — a leaf's outline, the holes of a fence,
    // cut out of two triangles by a picture. Not in the colour, not in who drew the pixel, not in the shadow it
    // casts, not for a ray. At 0 the surface is whole whatever `opacity` says.
    float cutout = 0;
    // Seen from behind too — a leaf, a sheet of cloth, a pane — its normal there turned to face the eye. Without
    // it a face turned away is not drawn (a closed thing's far side never shows).
    bool two_sided = false;
    // It lets what is behind it show, by the share it does not hide (1 − `opacity` × its picture's cover): a pane
    // of glass, a label on it, smoke on a card. Such surfaces are laid over the frame after everything whole, each
    // pixel's from the farthest to the nearest whatever order they were drawn in, and each keeps its name and its
    // share of the pixel (`Targets::through`). The pixel's depth, normal, velocity and `ids` stay those of what
    // stops the eye behind them. It hides nothing from a light: it casts no shadow.
    bool see_through = false;
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
// are about a pixel across, mixed with the next, so a pattern does not shimmer or band where it is small; at a slant several places along
// the pixel's strip are taken (`over`).
// What a picture's bytes are: colours as a display shows them (sRGB: the display's curve is taken off once), or
// plain numbers — a normal, a roughness — kept as they are (a byte ÷ 255).
enum class Coding : std::uint8_t { colour, plain };
struct Texture {
    int width = 0, height = 0;
    std::vector<Vec3>        light;   // every size's texels, a row after a row, the finest size first
    std::vector<std::uint32_t> sizes; // where each size begins in `light`
    // How much of what is behind it each texel hides (the picture's fourth byte ÷ 255), laid out as `light` is —
    // or empty: every texel hides all. Where it is there `light` holds each texel's light times its cover, so the
    // mean of four texels, and what lies between texels, is the light of what is there and not of the clear
    // texels beside it (no dark rim about a leaf).
    std::vector<float>       cover;
    // From rgba8 (`width` × `height` × 4, rows from the top), colours in sRGB unless said plain. Nothing: no size,
    // no pixels, or too few bytes. A picture of colours with a fourth byte below 255 anywhere has a `cover`.
    [[nodiscard]] static std::optional<Texture> made(std::span<const std::uint8_t> rgba, int width, int height, Coding coding = Coding::colour);
    // Its light at `uv` (0 … 1 across and down, repeating), for a pixel that covers `footprint` texels of the finest
    // size across. Between four texels of a size, between two sizes. The same bits everywhere.
    [[nodiscard]] Vec3 at(Vec2 uv, float footprint) const noexcept;
    // Its light over what a pixel covers of it: `right` and `down` are how far the surface's place in the picture
    // moves from this pixel to the next across and to the next below. Seen at a slant a pixel covers a long
    // strip: up to eight places along the strip's length are taken, each at the size the strip's width asks —
    // so a floor stays sharp across the way it is looked along instead of going soft both ways.
    [[nodiscard]] Vec3 over(Vec2 uv, Vec2 right, Vec2 down) const noexcept;
    // Both at once: the light as `over` gives it, and how much the picture hides there (1 where it has no cover).
    struct Seen {
        Vec3  light;
        float cover = 1;
    };
    [[nodiscard]] Seen seen(Vec2 uv, Vec2 right, Vec2 down) const noexcept;
    // How much it hides at a place alone, between the four texels of its finest size: what a ray asks.
    [[nodiscard]] float cover_at(Vec2 uv) const noexcept;
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

// What is about the scene as a picture of light (an HDR environment: a sky photographed, a studio's lamps),
// kept so that every question a surface asks of it is one lookup — on the CPU and, the same numbers, on a device:
//   · `sizes[0]`: the picture, for a way looked along (`toward`) — what a ray into the open and a mirror see;
//   · `sizes[n]`: it as a surface of roughness n / (sizes − 1) mirrors it (`blurred`);
//   · `falls`: what falls on a matt surface about each normal (`falling`): every way's light by how it faces;
//   · `shares`: how much of an even light a surface mirrors, by how it is looked at and how rough it is (`share`).
// Each picture is a square: the eight faces of an octahedron about the scene unfolded (y up: the upper four the
// diamond in the middle). A way's place in it is three divisions — no angle is taken, so the reference's lookup is
// the same bits everywhere and a shader's is the same arithmetic; one flat picture serves a device with no cube
// maps to spare. Made once from the picture as it comes (`made`), by deterministic sums — no grain, the same
// bits everywhere: a device is handed these numbers and gathers nothing itself.
struct Wrapping {
    int   side = 0;        // how far across `sizes[0]` is: a power of two, 16 … 4096; 0: as fine as the picture, at most 1024
    int   sizes = 11;      // how many roughnesses, evenly from 0 to 1; each as far across as its highlight is narrow
    int   samples = 64;    // the ways taken about each texel of a narrow highlight's size (a wide one is summed over every way)
    float turn = 0;        // the picture turned about the way up, radians: its middle column is toward +x, then on toward +z
    float strength = 1;    // every light times this
    float most = 65504.0f; // no light above this (what a half float holds); how many texels were cut: `clamped`
};
struct Surroundings {
    struct Square {
        int side = 0;
        std::vector<Vec3> light;  // side × side, a row after a row
    };
    std::vector<Square> sizes;
    Square              falls;
    int                 shares_side = 0;
    std::vector<Vec2>   shares;  // shares_side × shares_side: a row a roughness (0 … 1), a column how it is looked at (the normal · the way to the eye, 0 … 1)
    std::uint64_t       clamped = 0;
    // For following light way by way: one of the picture's sizes (at most 256 across) as a running sum of each
    // texel's light by its share of the ways, so that a way is chosen as often as there is light along it.
    int                 chosen_side = 0;
    std::vector<double> chosen_up_to;
    // From a picture a row an angle down from straight up and a column an angle about (equirectangular, as HDR
    // environments come; `mlibs.image`'s `Light` is one): `width` × `height` × 3 floats, rows from the top.
    // Nothing: no size, or too few numbers.
    [[nodiscard]] static std::optional<Surroundings> made(std::span<const float> rgb, int width, int height, const Wrapping& how = {});
    [[nodiscard]] Vec3 toward(Vec3 way) const noexcept;                   // `way` of any length but none
    [[nodiscard]] Vec3 blurred(Vec3 way, float roughness) const noexcept;
    [[nodiscard]] Vec3 falling(Vec3 normal) const noexcept;
    [[nodiscard]] Vec2 share(float seen, float roughness) const noexcept; // (times what is mirrored head on, added)
    // A way chosen by two numbers of 0 … 1, likelier the more light the picture has there, and how likely it
    // was (a way's share of all ways: over every way it sums to 1; 0 where the picture has no light) — what a
    // tracer weighs a small sun by instead of waiting to meet it.
    struct Chosen {
        Vec3  way;
        float likelihood = 0;
    };
    [[nodiscard]] Chosen chosen(float first, float second) const noexcept;
    [[nodiscard]] float likelihood(Vec3 way) const noexcept;
};

// What is about the scene. The least it can be: a sky above, the land at eye level, the ground below — three
// colours, and a way looked along is between two of them by how far up or down it goes; a surface with a
// highlight mirrors it, a matt one does not ask it (it takes `Options::ambient`). With a `picture` the picture
// is asked instead, by every surface: what falls on a matt one is the picture's (`ambient` is not added), a
// mirroring one takes the picture blurred by its roughness, and a ray into the open sees the picture itself.
// Off: nothing is mirrored but the lights.
struct Environment {
    bool on = false;
    Vec3 above{0.35f, 0.5f, 0.8f}, level{0.6f, 0.6f, 0.6f}, below{0.15f, 0.13f, 0.1f};
    const Surroundings* picture = nullptr;  // not owned: it outlives what is drawn with it
    // Where nothing is drawn: the picture as the eye sees it there (what a viewer calls its skybox), through
    // the fog as the background is. false, or no picture: `Options::background`.
    bool behind = false;
    [[nodiscard]] constexpr Vec3 toward(Vec3 way) const noexcept {  // `way` of length 1
        if (picture) return picture->toward(way);
        const float up = way.y > 0 ? way.y : -way.y;
        const Vec3 far = way.y > 0 ? above : below;
        return level + (far - level) * up;
    }
    // What falls on a surface from all about its normal (the mean of the ways, each by how it faces).
    [[nodiscard]] constexpr Vec3 falling(Vec3 normal) const noexcept { return picture ? picture->falling(normal) : toward(normal); }
};
// What a surface mirrors of the environment toward the eye: what lies along the eye's way turned about the
// normal, less sharply the rougher it is, by how much the surface mirrors there. With a picture: the picture
// blurred by the roughness, times the surface's share of an even light (`Surroundings::share`) — the two taken
// apart, as Karis did in 2013; how far that is from the light followed way by way is measured (the README).
[[nodiscard]] Vec3 mirrored(const Material& material, const Environment& environment, Vec3 normal, Vec3 toward_eye) noexcept;
// How much of an even light from all about a surface mirrors at the first of its small faces the light meets,
// by how it is looked at (`seen`: the normal · the way to the eye) and how rough it is: (times what it mirrors
// head on, added whatever that is). Their sum is under 1 — the rougher the further (a third at the roughest,
// looked at straight): the rest met a second face. One table for every surface (`share_table`: 32 × 32, a row a
// roughness, a column how it is looked at), summed once from the tracer's own highlight.
[[nodiscard]] Vec2 share(float seen, float roughness) noexcept;
[[nodiscard]] std::span<const Vec2> share_table() noexcept;
inline constexpr int share_table_side = 32;
// What the highlight is multiplied by so that light is kept between a rough surface's small faces: what met a
// second face is sent on as what the surface mirrors head on would have it, 1 + head on × (1 ÷ the share's sum − 1).
// A white metal sends back all of an even light however rough it is (the furnace's rule); a surface that
// mirrors 4% is all but as it was. In `reflected`, in `mirrored` under a picture, and along the tracer's rays.
[[nodiscard]] Vec3 kept(const Material& material, float seen) noexcept;

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
    // How much of the light from everywhere reaches a surface: with `occlusion_rays` above 0 (and the assets'
    // trees) that many rays leave each sample about its normal, and `ambient` is given only the share of them
    // that go `occlusion_reach` without meeting anything — a corner, the floor under a thing, darker. The rays
    // are a pixel's own (its number and `occlusion_seed`): grain, which frames gathered over time take out when
    // the seed changes with the frame. The reference a device's occlusion from depth is held to.
    std::uint32_t occlusion_rays = 0;
    float         occlusion_reach = 4;
    std::uint64_t occlusion_seed = 1;
    Environment environment;
};

inline constexpr std::uint32_t none = 0xffffffffu;
// Who drew a pixel: the draw (its place in Targets::draws), and in its mesh the cluster and the triangle.
struct Pixel {
    std::uint32_t draw = none, cluster = none, triangle = none;
};
// A surface seen through, at a pixel: who, how deep, and its share of the pixel's light — what it hides of what is
// behind it, times what the ones nearer than it let through. What is left over of 1 is what stops the eye's.
struct Through {
    std::uint32_t pixel = 0;  // a row after a row from the top
    Pixel         id;
    float         depth = 0;
    float         share = 0;
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
    std::vector<Through>      through;   // the surfaces seen through, in pixel order; of one pixel, the nearest first
    void resize(int new_width, int new_height);
};
struct Stats {
    std::uint32_t draws = 0, missing = 0;                   // drawn; naming no mesh or no material
    std::uint32_t clusters = 0, clusters_outside = 0, clusters_away = 0;
    std::uint64_t triangles = 0, facing_away = 0, outside = 0, clipped = 0, filled = 0;
    std::uint64_t written = 0, hidden = 0;                  // pixels that passed the depth, and that did not
    std::uint64_t cut = 0;                                  // samples a cut-out left out (`Material::cutout`): before the depth, so not in either
    std::uint64_t through = 0;                              // samples of surfaces seen through, laid over what is behind them (not in `written`)
    std::uint64_t rays = 0, shadowed = 0;                   // rays sent toward a light, and those that met something
    std::uint64_t occlusion_rays = 0, occluded = 0;         // rays sent for the light from everywhere, and those that met something
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
    const Material*          material = nullptr;  // where it cuts the surface out, with its picture: what a ray passes through
    const Texture*           picture = nullptr;
};
// What a ray meets first of a mesh (through its tree, as mlibs.geom's `meet`), the surface being `material`'s:
// where the material cuts it out (`Material::cutout`, by `opacity` and `picture`'s cover at the place met) the ray
// goes on through. `material` none, or one that cuts nothing out: geom's answer as it is.
[[nodiscard]] std::optional<mlibs::geom::RayHit> meet(const mlibs::geom::Ray& ray, const mlibs::geom::Mesh& mesh, const mlibs::geom::Bvh& tree, const Material* material, const Texture* picture,
                                                      float farthest = std::numeric_limits<float>::max());

// A sample of a surface seen through, as a frame keeps it until the pixel's are laid over one another.
struct Laid {
    Through what;   // `share`: how much it hides, until then
    Vec3    light;
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
    std::vector<Laid>   laid_;         // a frame's samples of surfaces seen through, kept between frames
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
// The surfaces seen through at the pixel, the nearest first (none: empty): each one's `id` names its draw as
// `Targets::ids` does.
[[nodiscard]] std::span<const Through> through_at(const Targets& targets, int x, int y) noexcept;

// For a frame's layer of this picture (xrender.frame's `PictureLayer`): who drew each pixel as one number — the draw's
// place and one, 0 where nothing drew — and what `frame.at` says of a pixel beside it, as JSON members
// (`"instance":11,"mesh":2,"material":2,"cluster":9,"triangle":640,"depth":0.93,"normal":[0,0.6,0.8]`), and where
// surfaces are seen through there, each with its share, the nearest first
// (`"through":[{"instance":30,"material":4,"share":0.4}]`).
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
// How light becomes what a display can show. `exposure` scales it first. With neither curve what is above the
// display's white is cut off there, each part by itself — a bright lamp goes flat and its colour with it.
// `filmic`: the light is bent toward white as it grows (the fitted curve games use, Narkowicz's of ACES: +, ×
// and ÷ only), so a highlight keeps its shape and a little of its colour — and everything below it is changed
// too. `neutral` (taken where both are said): Khronos' PBR Neutral (2024), what a viewer of products shows by —
// light up to 0.76 is as it is but for a small lift off black, so a colour is the material's own; above, the
// brightest part is drawn toward 1 without reaching it and the colour toward white by how much was taken.
struct Tone {
    float exposure = 1;
    bool  filmic = false;
    bool  neutral = false;
    [[nodiscard]] constexpr Vec3 of(Vec3 light) const noexcept {
        const Vec3 scaled = light * exposure;
        if (neutral) {
            const auto least_of = [](float one, float other) { return one < other ? one : other; };
            const auto most_of = [](float one, float other) { return one > other ? one : other; };
            const Vec3 held{most_of(scaled.x, 0.0f), most_of(scaled.y, 0.0f), most_of(scaled.z, 0.0f)};
            const float least = least_of(held.x, least_of(held.y, held.z));
            const float lift = least < 0.08f ? least - 6.25f * least * least : 0.04f;
            const Vec3 lifted{held.x - lift, held.y - lift, held.z - lift};
            const float peak = most_of(lifted.x, most_of(lifted.y, lifted.z));
            constexpr float begins = 0.8f - 0.04f, room = 1.0f - begins, fades = 0.15f;
            if (peak < begins) return lifted;
            const float drawn = 1.0f - room * room / (peak + room - begins);
            const Vec3 bent = lifted * (drawn / peak);
            const float toward_white = 1.0f - 1.0f / (fades * (peak - drawn) + 1.0f);
            return bent + (Vec3{drawn, drawn, drawn} - bent) * toward_white;
        }
        if (!filmic) return scaled;
        const auto bent = [](float value) { return value <= 0 ? 0.0f : (value * (2.51f * value + 0.03f)) / (value * (2.43f * value + 0.59f) + 0.14f); };
        return {bent(scaled.x), bent(scaled.y), bent(scaled.z)};
    }
};
// The frame as shown: the light, what of it spread, the tone, the display's curve. The spread is three box passes each
// way (close to a bell), in a fixed order: the same bits everywhere. What would spread from beyond the frame's
// edge is not there, and what spreads past it is lost. Keeps its working space between frames.
class Glow {
public:
    struct Told {
        std::uint32_t bright = 0;  // pixels with something to spread
    };
    // `colour` becomes rgba8, sRGB, `width` × `height`. With no radius it is the light through the curve.
    Told show(std::span<const Vec3> light, int width, int height, const Bloom& bloom, std::vector<std::uint8_t>& colour, const Tone& tone = {});
    [[nodiscard]] std::span<const Vec3> spread() const noexcept { return spread_; }  // the last frame's, before `strength`

private:
    std::vector<Vec3> spread_, other_;
};

// Light followed along rays: the reference of what the pass's constant "light from everywhere" stands in for
// (the foundation plan's §5.7, D-184 step c). Each pixel sends `samples` rays; where one meets a surface, the
// surface's light is what it gives off, what the lights send it (as the pass works it out, shadows by rays), and
// what arrives from one more way chosen about its normal — followed in turn, `bounces` times, and at the end or
// into the open the sky (`Options::environment` where it is on, else the background). So a corner is darker than
// an open floor, and a floor beside a red wall is a little red, with nothing told to make it so.
//
// Slow and deterministic: a pixel's rays come from its number, the sample's and `seed` alone — the same bits
// everywhere, in whatever order pixels are drawn. `Options::ambient` is not used: this is what it approximates.
// A surface with a highlight is followed two ways, as often each as it is a mirror: about its normal, or thrown
// back by one of its small faces — so it mirrors what is about it (a wet floor, the things standing on it). An instance that does not `cast` is seen by the eye
// only — a lamp's globe about its light.
struct Trace {
    std::uint32_t samples = 16;
    std::uint32_t bounces = 2;   // how many times a ray is followed on from a surface
    std::uint64_t seed = 1;
};
struct Traced {
    std::uint64_t rays = 0;      // every ray sent: from the eye, on from a surface, toward a light
    std::uint64_t into_open = 0; // those that met nothing and took the sky's light
};
// `out.light` and `out.colour` (the light through the display's curve) of `out`'s size; the other targets are
// not written. Needs `assets.trees`: a mesh with no tree is not there.
Traced traced(const scene::Scene& scene, const scene::View& view, const Assets& assets, Targets& out, const Options& options = {}, const Trace& how = {});

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
