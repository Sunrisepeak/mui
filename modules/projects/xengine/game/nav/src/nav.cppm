// xengine.nav — navigation for games (M6.4; D-161, D-176): Recast & Detour (zlib, through mwrap's
// mcpplibs.mwrap.recastnavigation) under this package's contract — a navigation mesh built in tiles from the
// ground's triangles for one size of walker, paths and rays across it, obstacles and links that build only the
// tiles they touch again, and a crowd of walkers stepped at a fixed step. The mesh, its obstacles and links and
// every walker are one world: saved, restored and branched to the same bits.
//
//     namespace nav = xengine::nav;
//     auto world = nav::World::build({.points = points, .corners = triangles}, {.radius = 0.4f});
//     const auto way = world->path({-5, 0, -5}, {5, 0, -5});   // corners, length, whether it reaches
//     const auto walker = *world->add({.at = {-5, 0, -6}});
//     world->target(walker, {5, 0, -5});
//     world->step();                                             // 1/60 s
//     const auto saved = world->save();                          // tiles, obstacles, links, every walker
//
// Determinism: the wrap and this package are built with no contraction and no fast math; a tile is a function of
// the triangles, the numbers and the obstacles and links over it; a path is planned whole in the step it is
// asked for (Detour's crowd would plan it across steps, in a queue nothing can save), and a link crossed is
// carried by this package. An image is valid for the same Recast & Detour version.
export module xengine.nav;

import std;
export import mlibs.math;

export namespace xengine::nav {

using Vec = mlibs::math::Vec3;

// The ground: triangles a walker may stand on (or not: walls and ceilings too), three corners a triangle,
// wound either way.
struct Ground {
    std::vector<Vec> points;
    std::vector<std::uint32_t> corners;
};
// The one size of walker a mesh is built for: its radius and height, the highest step it climbs, the steepest
// slope it walks (radians).
struct Walker {
    float radius = 0.4f;
    float height = 1.8f;
    float climb = 0.4f;
    float slope = 0.785398f;  // 45°
};
struct Options {
    float cell = 0.2f;          // the voxels' size across (m)
    float cell_height = 0.1f;   // and up
    int tile = 48;              // a tile's side in cells
    float step = 1.0f / 60.0f;  // a crowd step's seconds
    int most_walkers = 128;
};

// A way a walker cannot walk (a jump, a ladder, a door it opens): from one place to another, both on the mesh
// within `radius`, both ways or one.
struct Link {
    Vec from, to;
    float radius = 0.5f;
    bool both_ways = true;
};
// Where no walker goes, kept its radius away: a standing cylinder (`at` its base) or a box (`at` its centre).
struct Obstacle {
    enum class Form : std::uint8_t { cylinder, box } form = Form::cylinder;
    Vec at;
    float radius = 0.5f, height = 2.0f;
    Vec half{0.5f, 0.5f, 0.5f};
    [[nodiscard]] static Obstacle cylinder(Vec base, float radius, float height) { return {.form = Form::cylinder, .at = base, .radius = radius, .height = height}; }
    [[nodiscard]] static Obstacle box(Vec centre, Vec half) { return {.form = Form::box, .at = centre, .half = half}; }
};

template <class Tag>
struct Id {
    std::uint32_t value = 0;
    bool operator==(const Id&) const = default;
    auto operator<=>(const Id&) const = default;
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
};
// The world's own numbers, 1 upwards in the order made: carried by an image.
using LinkId = Id<struct LinkTag>;
using ObstacleId = Id<struct ObstacleTag>;
using WalkerId = Id<struct WalkerTag>;

// A place on the mesh, and the polygon it is on (Detour's reference: valid until its tile is built again).
struct Place {
    Vec at;
    std::uint32_t polygon = 0;
};
// The way from one place to another: the corners a walker turns at (the first where it starts, the last where it
// ends; `onto_link` marks a corner where a link begins), its length, whether it reaches the place asked (or
// ends as near as it can get), and the polygons it crosses.
struct Path {
    std::vector<Vec> corners;
    std::vector<bool> onto_link;
    float length = 0;
    bool reaches = false;
    std::vector<std::uint32_t> polygons;
};
// A straight walk along the mesh: how far it goes (1: all the way), where it stops and the wall's normal there.
struct RayHit {
    float fraction = 1;
    Vec at, normal;
    bool clear = true;
};

// A walker of the crowd: where it starts, its size, its speed and acceleration, how hard it keeps apart.
struct WalkerDef {
    Vec at;
    float radius = 0.4f, height = 1.8f;
    float speed = 3.5f, acceleration = 8.0f;
    float separation = 2.0f;
};
enum class Going : std::uint8_t {
    idle,         // no target
    walking,      // toward its target
    arrived,      // within its radius of the target
    unreachable,  // toward the nearest it can get to a target it cannot reach
    crossing,     // carried along a link
    off_mesh,     // not on the mesh (its ground built away): waits
};
struct WalkerState {
    WalkerId id;
    Vec at, velocity;
    Going going = Going::idle;
    std::optional<Vec> target;
    std::vector<Vec> ahead;  // the next corners it steers for (at most four)
    WalkerDef def;
};

enum class Errc : std::uint8_t { ground, walker, place, state, limit };
struct Error {
    Errc code = Errc::ground;
    std::string text;
};

class World {
public:
    // The mesh built in tiles from `ground` for `walker`; `links` joined as it is built.
    static std::expected<World, Error> build(const Ground& ground, const Walker& walker = {}, const Options& options = {}, std::span<const Link> links = {});
    // The world anew from other ground (an agent's build, a level edited), its frame count kept: its obstacles,
    // links and walkers go with the old mesh.
    std::expected<void, Error> rebuild(const Ground& ground, const Walker& walker);
    // A world from an image alone.
    static std::expected<World, Error> from(std::span<const std::uint8_t> image);
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    ~World();

    // ── asked ─────────────────────────────────────────────────────────
    // The nearest place on the mesh within `reach` (half extents) of `at`.
    [[nodiscard]] std::optional<Place> nearest(Vec at, Vec reach = {2, 4, 2}) const;
    [[nodiscard]] std::expected<Path, Error> path(Vec from, Vec to) const;
    [[nodiscard]] std::expected<RayHit, Error> ray(Vec from, Vec to) const;
    [[nodiscard]] int tiles() const noexcept;
    [[nodiscard]] int polygons() const noexcept;
    [[nodiscard]] const Walker& walker_size() const noexcept;

    // ── obstacles and links: the tiles they touch built again ─────────
    std::expected<ObstacleId, Error> place(const Obstacle& obstacle);
    void take(ObstacleId obstacle);
    [[nodiscard]] std::span<const ObstacleId> obstacles() const noexcept;
    [[nodiscard]] std::optional<Obstacle> obstacle(ObstacleId obstacle) const noexcept;
    std::expected<LinkId, Error> join(const Link& link);
    void unjoin(LinkId link);
    [[nodiscard]] std::span<const LinkId> links() const noexcept;
    [[nodiscard]] std::optional<Link> link(LinkId link) const noexcept;

    // ── the crowd ─────────────────────────────────────────────────────
    std::expected<WalkerId, Error> add(const WalkerDef& walker);
    void remove(WalkerId walker);
    // Sent to the nearest place on the mesh to `to`, its path planned whole now.
    std::expected<void, Error> target(WalkerId walker, Vec to);
    void stop(WalkerId walker);
    [[nodiscard]] std::optional<WalkerState> walker(WalkerId walker) const;
    [[nodiscard]] std::span<const WalkerId> walkers() const noexcept;
    // One step: every walker steered along its corridor, kept apart, moved on the mesh; across a link, carried.
    void step();
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }

    // ── a value ───────────────────────────────────────────────────────
    // A step boundary's image: the ground and the numbers, each tile's bytes and generation, the obstacles and
    // links, every walker; the same state, the same bytes.
    [[nodiscard]] std::vector<std::uint8_t> save() const;
    // Back to an image: an image refused (cut short, damaged, another version's) leaves the world as it was.
    std::expected<void, Error> restore(std::span<const std::uint8_t> image);
    [[nodiscard]] std::expected<World, Error> branch() const { return from(save()); }
    [[nodiscard]] std::uint64_t state_hash() const;

private:
    friend class Direct;
    struct Native;
    World();
    std::unique_ptr<Native> native_;
    std::vector<ObstacleId> obstacles_;
    std::vector<LinkId> links_;
    std::vector<WalkerId> walkers_;
    std::uint64_t steps_ = 0;
};

// Detour's crowd called directly on a world's mesh and walkers, for comparing costs (tests/bench): it plans paths
// in its own queue across steps, so its walkers do not go the same way as the world's, bit for bit.
class Direct {
public:
    [[nodiscard]] static std::expected<Direct, Error> of(const World& world);
    Direct(Direct&&) noexcept;
    Direct& operator=(Direct&&) noexcept;
    ~Direct();
    void step() noexcept;
    [[nodiscard]] std::size_t walkers() const noexcept;
    [[nodiscard]] Vec at(std::size_t walker) const noexcept;

private:
    Direct() = default;
    struct Native;
    std::unique_ptr<Native> native_;
};

}  // namespace xengine::nav
