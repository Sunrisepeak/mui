// xrender.asset — things made elsewhere, brought in (the foundation plan's §5.5, M6.3; D-171).
//
//     const auto model = asset::gltf(bytes);                       // a .gltf's text or a .glb's bytes
//     if (!model) return fail(model.error());
//     asset::place(*model, world, {.mesh = first_mesh, .material = first_material});
//     for (const asset::Note& note : model->notes) log(note.where, note.what, note.instead);
//
// A glTF 2.0 document becomes what the scene and the mesh pass are given: a mesh in clusters a primitive, the
// materials, the pictures as textures, an instance for each primitive of each node of the scene (its id the
// file's place: the node's number and the primitive's, the same in every run), the lights. What the document says
// that this does not carry — a normal map, a skin, an extension — is not dropped in silence: each is a `Note`
// with where it is in the document (a JSON pointer), what it is, and what was done instead.
//
// Nothing of a file is trusted: its indices, counts and places are checked (by the parser's validation, then by
// `mlibs.geom`'s `check` on every mesh made), and a document that cannot be read is refused with its reason.
export module xrender.asset;

import std;
export import xrender.mesh;

export namespace xrender::asset {

using mlibs::math::Mat4;
using xrender::scene::Id;

// What the document said that is not in what was made.
struct Note {
    std::string where;    // a JSON pointer into the document: /materials/2/normalTexture
    std::string what;     // what is there
    std::string instead;  // what was done: left out, or what stands in its place
};

// An instance's place in the document.
struct Placed {
    std::uint32_t node = 0, mesh = 0, primitive = 0;  // glTF's numbers
    std::string   name;                               // the node's, or none
};

// What moves, read as numbers and not played (what is drawn stays at rest): the nodes as a hierarchy, the skins
// and the animations as the document has them. Their owner (hanim, D-93; hanim.skeleton's `gltf`) makes skeletons
// and clips of them.
struct Node {
    std::string       name;         // the node's, or none
    std::int32_t      parent = -1;  // a node's number; −1: a root
    mlibs::math::Vec3 translation{0, 0, 0};
    mlibs::math::Quat rotation{};
    mlibs::math::Vec3 scale{1, 1, 1};
    Mat4              local;           // the same place as a matrix (the document's own where it gives one)
    bool              matrix = false;  // the document gives a matrix, not the three (such a node is not animated)
};
struct Skin {
    std::string                  name;
    std::vector<std::uint32_t>   joints;         // nodes
    std::vector<Mat4>            inverse_binds;  // one a joint (the identity where the document gives none)
    std::optional<std::uint32_t> skeleton;       // the node the document names as its root
};
enum class Path : std::uint8_t { translation, rotation, scale, weights };
enum class Interpolation : std::uint8_t { linear, step, cubic };
struct Channel {
    std::uint32_t      node = 0;
    Path               path = Path::translation;
    Interpolation      interpolation = Interpolation::linear;
    std::vector<float> times;   // seconds, rising
    // A key's numbers one after another: 3 (translation, scale), 4 (rotation: x, y, z, w) or a weight a morph
    // target; for `cubic`, three such a key: the tangent in, the value, the tangent out.
    std::vector<float> values;
};
struct Animation {
    std::string          name;
    std::vector<Channel> channels;  // a channel whose sampler cannot be read is a note, and not here
};

// An instance a skin bends: each frame its mesh (`Instance::mesh`, made at rest) is `rest` bent by the skin's
// matrices — xrender.mesh's `bend(imported.meshes[rest], skin_matrices, its mesh, room)` — and its place stays the
// identity (the joints place it). `rest` holds the joints and weights; it is not itself placed.
struct Skinning {
    std::uint32_t instance = 0;  // in `Imported::instances`
    std::uint32_t skin = 0;      // in `Imported::skins`
    std::uint32_t rest = 0;      // in `Imported::meshes`
};

struct Imported {
    std::vector<mlibs::geom::ClusteredMesh> meshes;     // one a primitive carried, in the document's order
    std::vector<mesh::Material>             materials;  // the document's, in its order; then the one a primitive with none is given
    std::vector<mesh::Texture>              textures;   // the pictures the materials name: colours (base, emission) and plain numbers (the surface's, the normal, shade)
    // `mesh` and `material` are places in the two above (`place` adds where its caller keeps them); `bounds` the
    // mesh's. In id order.
    std::vector<scene::Instance> instances;
    std::vector<Placed>          placed;  // one an instance, in the same order
    std::vector<scene::Light>    lights;  // KHR_lights_punctual's, a node each
    std::vector<std::string>     material_names;
    std::vector<Node>            nodes;  // every node of the document, by its number
    std::vector<Skin>            skins;
    std::vector<Skinning>        skinned;  // in instance order
    std::vector<Animation>       animations;
    std::vector<Note>            notes;
    // Triangles in all the meshes, and the instances' box in the world.
    [[nodiscard]] std::size_t triangles() const noexcept;
    [[nodiscard]] mlibs::geom::Box3 bounds() const noexcept;
};

// An instance's id: `base` + (node + 1) × 65,536 + primitive + 1. A light's: `base` + node + 1.
inline constexpr std::uint64_t ids_a_node = 65536;

struct Options {
    // A file the document names beside itself (a buffer, a picture), by the name as the document has it. None:
    // such a file is a reason to refuse (a buffer) or a note (a picture).
    std::function<std::expected<std::vector<std::uint8_t>, std::string>(std::string_view name)> read;
    // Each mesh's triangles put in the order that draws them fastest and cut into clusters of triangles that lie
    // together (`optimised`). Off: in the document's order, cut as they come.
    bool optimise = true;
    Id   base = 0;  // added to every id made
    // Which scene of the document: its own choice, or the first. One it does not have is refused.
    std::optional<std::uint32_t> scene;
};

// The document — a .gltf's text or a .glb's bytes — as what a scene is made of, or why it is not one.
[[nodiscard]] std::expected<Imported, std::string> gltf(std::span<const std::uint8_t> bytes, const Options& options = {});
// The same from a file; what it names beside itself is read from its directory (unless `options.read` is given).
[[nodiscard]] std::expected<Imported, std::string> gltf_file(const std::filesystem::path& file, Options options = {});

// Where a caller keeps what was brought in: the numbers its first mesh and first material have there.
struct Base {
    std::uint32_t mesh = 0, material = 0;
};
// Its instances and lights added to `scene`. Refused whole — the scene as it was — where one cannot be added (an
// id already there: bring it in with another `Options::base`).
[[nodiscard]] std::expected<void, std::string> place(const Imported& imported, scene::Scene& scene, Base base = {});

// A mesh's triangles ordered and cut for drawing: the order a vertex cache serves best, then clusters of up to
// `mlibs.geom`'s 124 triangles that lie together and face alike (meshoptimizer's meshlets, closed early where one
// would grow wide), their corners in the order the clusters use them. The same triangles; only their order
// changes, and a corner no triangle uses is left out. A cluster's bounds and cone are `mlibs.geom`'s own
// (`clustered` by runs). Refused where `mlibs.geom`'s `check` refuses it.
[[nodiscard]] std::expected<mlibs::geom::ClusteredMesh, std::string> optimised(mlibs::geom::Mesh mesh);

// A model looked at as a viewer of products frames it (model-viewer's orbit, and its render-fidelity scenarios'):
// the eye `radius` from `target`, `theta` about the way up from +z toward +x and `phi` down from the way up
// (radians), looking at the target, `upright` its angle of view up and down.
struct Orbit {
    float            theta = 0, phi = 1.5707964f, radius = 1;
    mlibs::math::Vec3 target{0, 0, 0};
    float            upright = 0.7853982f;
};
// The view of it: the near and far cuts set about `bounds` as the eye sees it.
[[nodiscard]] scene::View view_of(const Orbit& orbit, const mlibs::geom::Box3& bounds, float aspect) noexcept;

// A model drawn by the reference, as a picture to stand beside another renderer's: lit by `surroundings` (and
// by the document's own lights), `samples` × `samples` samples a pixel, through `tone`. The picture is
// straight: a pixel's colour is the mean of its samples that drew anything, and its fourth byte how many did —
// where nothing is, nothing (as such a viewer's picture with no background is).
struct Look {
    int                       width = 768, height = 768;
    int                       samples = 2;  // each way, 1 … 4
    const mesh::Surroundings* surroundings = nullptr;
    mesh::Tone                tone{.neutral = true};
    // Where the model is not: the surroundings as the eye sees them (a viewer's skybox), and the picture whole —
    // every pixel's fourth byte 255. false: nothing there.
    bool                      behind = false;
};
struct Looked {
    int                       width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    mesh::Stats               stats;
};
[[nodiscard]] std::expected<Looked, std::string> looked(const Imported& imported, const scene::View& view, const Look& how = {});

}  // namespace xrender::asset
