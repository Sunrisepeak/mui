// xengine.dynamics.rigid3 — the 3D rigid family (M6.4; D-170, D-176): Jolt Physics (MIT, through mwrap's
// mcpplibs.mwrap.jolt) under this package's contract, the 2D family's shape in three dimensions — numbers in
// (a material's mechanical columns), a fixed step whose output is what touched what, ids that stay the same
// across a restore and a branch, the state as bytes, its hash; rays and shapes asked of the world. The feature
// `rigid3`: a consumer that does not take it links no Jolt.
//
//     namespace rigid3 = xengine::dynamics::rigid3;
//     auto world = rigid3::World::make({.gravity = {0, -9.81f, 0}});
//     const auto crate = world->add({.at = {0, 4, 0}}, rigid3::Shape::box({0.5f, 0.5f, 0.5f}), {.density = 600});
//     world->step();                                   // 1/60 s, one collision step
//     for (const rigid3::Contact& contact : world->contacts()) { … }   // what began and ended touching in it
//     const auto saved = world->save();                // a step boundary: every body, its shape, Jolt's state
//     auto branch = world->branch();                   // a world of its own from here
//     world->restore(saved);                           // back: every id still names its body
//
// Determinism: Jolt is built cross-platform deterministic (JPH_CROSS_PLATFORM_DETERMINISTIC, no contraction, no
// fast math): the same bits on x86-64, arm64 and wasm32, with any number of workers. A saved state is this
// package's image around Jolt's own state stream: valid for the same Jolt version, the way a rollback, an
// undo and a branch use it within a run.
export module xengine.dynamics.rigid3;
import std;
export import mlibs.math;
import xengine.matter;

export namespace xengine::dynamics::rigid3 {

// mlibs.math's: one definition for every family and what couples them.
using Vec = mlibs::math::Vec3;
using Turn = mlibs::math::Quat;

enum class Kind : std::uint8_t { fixed, moving, kinematic };
// A body's mechanical numbers: density in kg per m³ (water's is 1000), friction, restitution — what
// material_of reads from xengine.matter's table.
struct Material {
    float density = 1000.0f;
    float friction = 0.6f;
    float restitution = 0.0f;
};
// A material's row of the matter table's mechanical columns (D-165); a column the table does not have keeps
// `base`'s, a material that is not the table's leaves `base` as it is.
[[nodiscard]] Material material_of(const xengine::matter::Program& matter, xengine::matter::MaterialId material, Material base = {}) noexcept;

struct Shape {
    enum class Form : std::uint8_t { box, sphere, capsule, hull, mesh } form = Form::box;
    Vec half{0.5f, 0.5f, 0.5f};          // box: half extents
    float radius = 0.5f;                 // sphere, capsule
    float half_height = 0.5f;            // capsule: half the length of its middle, along y
    std::vector<Vec> points;             // hull: the points it wraps; mesh: its corners
    std::vector<std::uint32_t> corners;  // mesh: three a triangle, wound counter-clockwise seen from outside
    [[nodiscard]] static Shape box(Vec half) { return {.form = Form::box, .half = half}; }
    [[nodiscard]] static Shape sphere(float radius) { return {.form = Form::sphere, .radius = radius}; }
    [[nodiscard]] static Shape capsule(float half_height, float radius) { return {.form = Form::capsule, .radius = radius, .half_height = half_height}; }
    [[nodiscard]] static Shape hull(std::vector<Vec> points) { return {.form = Form::hull, .points = std::move(points)}; }
    // A triangle mesh: terrain and buildings, on a fixed body only.
    [[nodiscard]] static Shape mesh(std::vector<Vec> points, std::vector<std::uint32_t> corners) { return {.form = Form::mesh, .points = std::move(points), .corners = std::move(corners)}; }
};
struct BodyDef {
    Kind kind = Kind::moving;
    Vec at;
    Turn turn;
    Vec velocity;
    Vec spin;                // radians a second about each axis
    bool bullet = false;     // swept against what it would pass through in a step (Jolt's linear cast)
};
// A body, the same in every world branched from the one it was made in, and across a restore.
// The world's own number, 1 upwards in the order made (0: none) — not Jolt's, so that an image carries it.
struct BodyId {
    std::uint32_t value = 0;
    bool operator==(const BodyId&) const = default;
    auto operator<=>(const BodyId&) const = default;
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
};
// Where a body is and how it moves: `at` its origin, `turn` its rotation, `centre` its centre of mass.
struct Pose {
    Vec at;
    Turn turn;
    Vec velocity, spin;
    bool awake = true;
    Vec centre;
    float mass = 0;  // 0: fixed
};
// Two bodies that began or ended touching in the last step, in the order of their ids (`one` < `other`): where,
// along which normal (from `one` toward `other`) and how fast they closed.
struct Contact {
    BodyId one, other;
    bool began = true;
    Vec point, normal;
    float approach = 0;  // speed along the normal as they met (began only)
};
struct RayHit {
    BodyId body;
    float fraction = 0;  // along the ray, 0 at its start, 1 at its end
    Vec point, normal;
};
struct Options {
    Vec gravity{0, -9.81f, 0};
    float step = 1.0f / 60.0f;
    int collision_steps = 1;  // Jolt's: collision steps a step (a substep each)
    int workers = 0;          // threads beside the caller's; the same bits with any number (the Web has none)
    std::uint32_t most_bodies = 65536;
};
enum class Errc : std::uint8_t { shape, body, state, limit };
struct Error {
    Errc code = Errc::body;
    std::string text;
};

class World {
public:
    [[nodiscard]] static std::expected<World, Error> make(const Options& options = {});
    World(World&&) noexcept;
    World& operator=(World&&) noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    ~World();

    [[nodiscard]] std::expected<BodyId, Error> add(const BodyDef& body, const Shape& shape, const Material& material = {});
    void remove(BodyId body) noexcept;
    [[nodiscard]] bool has(BodyId body) const noexcept;
    [[nodiscard]] std::optional<Pose> pose(BodyId body) const noexcept;
    // In the order made: what a hash and a save go through.
    [[nodiscard]] std::span<const BodyId> bodies() const noexcept;
    // An impulse (N·s) at a point in the world, or through the centre of mass.
    void push(BodyId body, Vec impulse, std::optional<Vec> at = std::nullopt) noexcept;
    void set_velocity(BodyId body, Vec velocity, Vec spin) noexcept;

    // One fixed step; what began and ended touching in it.
    void step() noexcept;
    [[nodiscard]] std::span<const Contact> contacts() const noexcept { return contacts_; }
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }
    [[nodiscard]] const Options& options() const noexcept { return options_; }

    // The first body a segment from `from` to `to` meets.
    [[nodiscard]] std::optional<RayHit> ray(Vec from, Vec to) const;
    // The bodies a shape placed at `at`, turned by `turn`, overlaps, each once, in the order of their ids.
    void overlapping(const Shape& shape, Vec at, Turn turn, std::vector<BodyId>& out) const;

    // The whole state at this step boundary: each body as it was made (its kind, its material, its shape's
    // bytes) and Jolt's state of all of them. Restored into this world, every id names its body again (one
    // removed since comes back, one added since goes); a world of its own is made from it.
    [[nodiscard]] std::vector<std::uint8_t> save() const;
    [[nodiscard]] std::expected<void, Error> restore(std::span<const std::uint8_t> saved);
    [[nodiscard]] std::expected<World, Error> branch() const;
    [[nodiscard]] static std::expected<World, Error> from(std::span<const std::uint8_t> saved, const Options& options = {});
    // Every body's pose and velocities, bit for bit (FNV-1a): what two runs compare.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    World() = default;
    struct Native;
    std::unique_ptr<Native> native_;
    std::vector<BodyId> bodies_;
    std::vector<Contact> contacts_;
    std::uint64_t steps_ = 0;
    Options options_;
};

// Jolt called directly on the same bodies — no ids of ours, no record of a body's making, no contact gathered:
// what a World's step is measured against, the wrapper's cost said as a number (the foundation plan's T2.9).
// For a benchmark; a game has a World.
struct Piece {
    BodyDef body;
    Shape shape;
    Material material;
};
class Direct {
public:
    [[nodiscard]] static std::expected<Direct, Error> make(std::span<const Piece> pieces, const Options& options = {});
    Direct(Direct&&) noexcept;
    Direct& operator=(Direct&&) noexcept;
    ~Direct();
    void step() noexcept;
    // Where piece `index` is: the two worlds are the same scene only while these agree.
    [[nodiscard]] Vec at(std::size_t index) const noexcept;

private:
    Direct() = default;
    struct Native;
    std::unique_ptr<Native> native_;
};

}  // namespace xengine::dynamics::rigid3
