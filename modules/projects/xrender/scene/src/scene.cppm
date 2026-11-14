// xrender.scene — what there is to draw in three dimensions, and what one view of it sees (the foundation
// plan's §5.1, M6.3; D-164).
//
//     scene::Scene world;
//     world.add({.id = 17, .mesh = crate, .material = wood, .world = translation({2, 0, -5}), .bounds = box});
//     scene::Extracted seen;
//     scene::extract(world, {.view = view, .projection = projection}, seen);   // draws, batches, lights
//     world.settle();                                                          // the frame is over
//
// Instances and lights are their ids — stable: their maker's numbers, the same in every run. Every edit leaves a
// change on a list until the frame is settled, so another representation of the scene (the tree here, an agent's
// projection, later whatever a lighting method needs) is built once and then follows. The extraction is a pure
// function of the scene and the view, the same bytes everywhere, and allocates nothing on a steady frame. No
// device and no pixels: a renderer's passes are given the extraction.
export module xrender.scene;

import std;
export import mlibs.math;
export import mlibs.geom;

export namespace xrender::scene {

using mlibs::geom::Box3;
using mlibs::geom::Frustum;
using mlibs::math::Mat4;
using mlibs::math::Vec3;

// An instance's or a light's name: its maker's number (a file's place, a path's hash), never 0, the same in every
// run. Instances and lights are numbered apart.
using Id = std::uint64_t;

struct Instance {
    Id            id = 0;
    std::uint32_t mesh = 0, material = 0;  // their owner's numbers: a batch is a run of one of each
    Mat4          world;
    Box3          bounds;                  // of its mesh, as the mesh has it (before `world`)
    std::uint32_t layers = 1;              // seen by a view with one of these bits
    bool          hidden = false;
    bool          casts = true;            // it stands between a light and what is behind it (a lamp's own glass does not)
};
enum class LightKind : std::uint8_t { sun, point, spot };
// How a light's numbers are read. `windowed`: its colour times its intensity, a point's or a spot's falling to
// nothing at its range as (1 − (d / range)²)². `physical` (glTF's KHR_lights_punctual): a sun's intensity in lux,
// a point's or a spot's in candela falling as the square of the distance — inside a window that ends it at its
// range, (1 − (d / range)⁴)² — and a spot's edge soft between `inner_cone` and `cone`.
enum class Falloff : std::uint8_t { windowed, physical };
struct Light {
    Id            id = 0;
    LightKind     kind = LightKind::sun;
    Vec3          colour{1, 1, 1};
    float         intensity = 1;
    Vec3          at;                  // a point's and a spot's place
    Vec3          toward{0, -1, 0};    // a sun's and a spot's direction
    float         range = 10;          // how far a point or a spot reaches
    float         cone = 0.5f;         // a spot's half angle, radians
    Falloff       falloff = Falloff::windowed;
    float         inner_cone = -1;     // a spot's half angle within which it is whole (radians; below 0: its edge is hard)
    std::uint32_t layers = 1;
    bool          hidden = false;
};
struct View {
    Mat4          view;        // the world to the camera (it looks down −z)
    Mat4          projection;  // the camera to clip space, depth 0 … 1
    std::uint32_t layers = 0xffffffffu;
};

enum class ChangeKind : std::uint8_t { added, removed, moved, changed, light_added, light_removed, light_changed };
struct Change {
    ChangeKind kind = ChangeKind::added;
    Id         id = 0;
};

enum class Errc : std::uint8_t { no_id, duplicate, absent, empty_bounds, not_finite };
[[nodiscard]] std::string_view text(Errc code) noexcept;

class Scene {
public:
    // Each is refused with its reason and leaves the scene as it was.
    [[nodiscard]] std::expected<void, Errc> add(const Instance& instance);
    [[nodiscard]] std::expected<void, Errc> remove(Id id);
    [[nodiscard]] std::expected<void, Errc> move(Id id, const Mat4& world);
    // Everything but where it is: its mesh, material, bounds, layers, whether it is hidden (`world` is kept).
    [[nodiscard]] std::expected<void, Errc> set(const Instance& instance);
    [[nodiscard]] std::expected<void, Errc> add(const Light& light);
    [[nodiscard]] std::expected<void, Errc> remove_light(Id id);
    [[nodiscard]] std::expected<void, Errc> set(const Light& light);

    // In id order, whatever order they were added in.
    [[nodiscard]] std::span<const Instance> instances() const noexcept { return instances_; }
    [[nodiscard]] std::span<const Light> lights() const noexcept { return lights_; }
    [[nodiscard]] const Instance* find(Id id) const noexcept;
    [[nodiscard]] const Light* find_light(Id id) const noexcept;
    // Where an instance is in the world: the box of its bounds moved. Of an id that is not there: empty.
    [[nodiscard]] Box3 world_bounds(Id id) const noexcept;

    // What was done since the last `settle`, in the order done.
    [[nodiscard]] std::span<const Change> changes() const noexcept { return changes_; }
    // The frame is over: the list is emptied, the revision moves on.
    void settle() noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Changes ever made, and how many of them are settled (no longer on the list): what a mirror reads to know
    // whether it missed any.
    [[nodiscard]] std::uint64_t changed() const noexcept { return settled_ + changes_.size(); }
    [[nodiscard]] std::uint64_t settled() const noexcept { return settled_; }

private:
    std::vector<Instance> instances_;
    std::vector<Light>    lights_;
    std::vector<Change>   changes_;
    std::uint64_t         revision_ = 0, settled_ = 0;
};

// The instances' boxes in the world as a tree: a scene's mirror, built once and then following its change list.
// Where things only moved the tree keeps its shape and its boxes are refitted; where something came or went it is
// made again. A tree refitted many times over things that moved far is a poor one (its boxes overlap): after so
// many refits a new one is made beside it a part a frame — `rebuild_things` things looked at each time it follows —
// and takes its place when it is whole, so no frame pays for the whole tree (counted in things, not in time: the
// same frames on every machine).
class Spatial {
public:
    enum class Followed : std::uint8_t { nothing, changes, rebuilt };
    // How many refits a tree's shape is kept for before a new one is begun, and how much of the new one a frame makes.
    static constexpr std::uint32_t most_refits = 64;
    static constexpr std::size_t   rebuild_things = 4096;
    // Brings it to the scene as it is now: by the changes since it last followed, or — made new, or having missed
    // a settled frame — from the whole scene. Call it after a frame's edits and before `settle`.
    Followed follow(const Scene& scene);
    // In the scene's id order: instance `index` here is `scene.instances()[index]`.
    [[nodiscard]] std::span<const Id> ids() const noexcept { return ids_; }
    [[nodiscard]] std::span<const Box3> boxes() const noexcept { return boxes_; }
    [[nodiscard]] const mlibs::geom::Bvh& tree() const noexcept { return tree_; }
    // Whether a new tree is being made beside the one in use, and how many times one has taken its place.
    [[nodiscard]] bool remaking() const noexcept { return remaking_.begun(); }
    [[nodiscard]] std::uint32_t remade() const noexcept { return remade_; }

private:
    void rebuild(const Scene& scene);
    [[nodiscard]] bool refit();
    std::vector<Id>   ids_;
    std::vector<Box3> boxes_;
    mlibs::geom::Bvh  tree_;
    std::uint64_t     followed_ = 0;  // Scene::changed() when it last followed
    std::uint32_t     refits_ = 0, remade_ = 0;
    bool              made_ = false;
    std::vector<Box3>     remaking_boxes_;  // the boxes as they were when the new tree was begun
    mlibs::geom::Bvh      spare_;           // the tree last replaced: the next one is made in its room
    mlibs::geom::BvhBuild remaking_;
};

struct Draw {
    Id            id = 0;
    std::uint32_t instance = 0;  // its place in Scene::instances()
    std::uint32_t mesh = 0, material = 0;
    float         depth = 0;     // of its box's middle, in front of the camera (behind it: below 0)
};
// A run of draws of one mesh and one material: what one instanced draw is made of.
struct Batch {
    std::uint32_t mesh = 0, material = 0;
    std::uint32_t first = 0, count = 0;  // in Extracted::draws
};
struct Extracted {
    std::vector<Draw>          draws;    // by material, then mesh, then nearest first, then id
    std::vector<Batch>         batches;
    std::vector<std::uint32_t> lights;   // places in Scene::lights(), in id order
    // Of the instances: how many were looked at, and why the others are no draw.
    std::uint32_t looked = 0, hidden = 0, other_layer = 0, outside = 0;
};
// What `view` sees of the scene. `out` is the caller's, reused: a steady frame allocates nothing.
void extract(const Scene& scene, const View& view, Extracted& out);
// The same by the tree: only what a box of it lets through is looked at (`looked`, `outside` count those).
// `spatial` has followed the scene.
void extract(const Scene& scene, const Spatial& spatial, const View& view, Extracted& out);

// Why an instance is no draw of a view, the first reason that holds.
enum class Unseen : std::uint8_t { seen, absent, hidden, other_layer, outside };
[[nodiscard]] Unseen why_unseen(const Scene& scene, const View& view, Id id) noexcept;
[[nodiscard]] std::string_view text(Unseen why) noexcept;

}  // namespace xrender::scene
