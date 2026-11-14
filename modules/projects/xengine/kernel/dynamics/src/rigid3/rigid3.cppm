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

struct Part;
struct Shape {
    enum class Form : std::uint8_t { box, sphere, capsule, hull, mesh, compound } form = Form::box;
    Vec half{0.5f, 0.5f, 0.5f};          // box: half extents
    float radius = 0.5f;                 // sphere, capsule
    float half_height = 0.5f;            // capsule: half the length of its middle, along y
    std::vector<Vec> points;             // hull: the points it wraps; mesh: its corners
    std::vector<std::uint32_t> corners;  // mesh: three a triangle, wound counter-clockwise seen from outside
    std::vector<Part> parts;             // compound: its parts, each placed in the body
    bool movable_parts = false;          // compound: its parts can be moved after it is made (World::move_part)
    // Defined after `Part`: a Shape made here holds a vector of parts, and libc++ wants its element whole.
    [[nodiscard]] static Shape box(Vec half);
    [[nodiscard]] static Shape sphere(float radius);
    [[nodiscard]] static Shape capsule(float half_height, float radius);
    [[nodiscard]] static Shape hull(std::vector<Vec> points);
    // A triangle mesh: terrain and buildings, on a fixed body only.
    [[nodiscard]] static Shape mesh(std::vector<Vec> points, std::vector<std::uint32_t> corners);
    // Several shapes as one body: fixed in place (Jolt's static compound), or movable (its mutable compound).
    [[nodiscard]] static Shape compound(std::vector<Part> parts, bool movable = false);
};
struct Part {
    Shape shape;
    Vec at;
    Turn turn;
};
inline Shape Shape::box(Vec half) { return {.form = Form::box, .half = half}; }
inline Shape Shape::sphere(float radius) { return {.form = Form::sphere, .radius = radius}; }
inline Shape Shape::capsule(float half_height, float radius) { return {.form = Form::capsule, .radius = radius, .half_height = half_height}; }
inline Shape Shape::hull(std::vector<Vec> points) { return {.form = Form::hull, .points = std::move(points)}; }
inline Shape Shape::mesh(std::vector<Vec> points, std::vector<std::uint32_t> corners) { return {.form = Form::mesh, .points = std::move(points), .corners = std::move(corners)}; }
inline Shape Shape::compound(std::vector<Part> parts, bool movable) {
    Shape made;
    made.form = Form::compound;
    made.parts = std::move(parts);
    made.movable_parts = movable;
    return made;
}
struct BodyDef {
    Kind kind = Kind::moving;
    Vec at;
    Turn turn;
    Vec velocity;
    Vec spin;                // radians a second about each axis
    bool bullet = false;     // swept against what it would pass through in a step (Jolt's linear cast)
    bool sensor = false;     // reports what overlaps it (as contacts) and pushes nothing; it senses moving bodies
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
    bool sensor = false;  // one of the two is a sensor: an overlap, nothing pushed
    Vec point, normal;
    float approach = 0;  // speed along the normal as they met (began only)
};
struct RayHit {
    BodyId body;
    float fraction = 0;  // along the ray, 0 at its start, 1 at its end
    Vec point, normal;
};
// A joint between two bodies (`other` none: the world), as data on the world: saved, restored and branched with
// the bodies. Places and axes are given in the world as the joint is made.
struct JointId {
    std::uint32_t value = 0;
    bool operator==(const JointId&) const = default;
    auto operator<=>(const JointId&) const = default;
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
};
// What drives a hinge or a slider: toward a speed (rad/s, m/s) or a place (rad, m), as hard as `most` allows
// (N·m, N), as a spring of `frequency` and `damping` when driving to a place.
struct Motor {
    enum class Mode : std::uint8_t { off, velocity, position } mode = Mode::off;
    float target = 0;
    float most = std::numeric_limits<float>::max();
    float frequency = 2, damping = 1;
};
struct Joint {
    enum class Form : std::uint8_t { fixed, hinge, slider, distance, point, cone } form = Form::point;
    BodyId one, other;
    Vec at;             // where they join (distance: on `one`)
    Vec to;             // distance: the point on `other`
    Vec axis{0, 1, 0};  // hinge: what it turns about; slider: what it slides along; cone: its middle
    // hinge: radians within [-π, 0] and [0, π]; slider and distance: metres. None given: free (distance: as made).
    float lower = -std::numeric_limits<float>::infinity(), upper = std::numeric_limits<float>::infinity();
    float half_angle = 0.5f;  // cone: how far from `axis` it may swing
    Motor motor;              // hinge, slider
    [[nodiscard]] static Joint fixed(BodyId one, BodyId other) { return {.form = Form::fixed, .one = one, .other = other}; }
    [[nodiscard]] static Joint hinge(BodyId one, BodyId other, Vec at, Vec axis) { return {.form = Form::hinge, .one = one, .other = other, .at = at, .axis = axis}; }
    [[nodiscard]] static Joint slider(BodyId one, BodyId other, Vec at, Vec axis) { return {.form = Form::slider, .one = one, .other = other, .at = at, .axis = axis}; }
    [[nodiscard]] static Joint distance(BodyId one, BodyId other, Vec at, Vec to) { return {.form = Form::distance, .one = one, .other = other, .at = at, .to = to}; }
    [[nodiscard]] static Joint point(BodyId one, BodyId other, Vec at) { return {.form = Form::point, .one = one, .other = other, .at = at}; }
    [[nodiscard]] static Joint cone(BodyId one, BodyId other, Vec at, Vec axis, float half_angle) {
        return {.form = Form::cone, .one = one, .other = other, .at = at, .axis = axis, .half_angle = half_angle};
    }
};
// A joint as it is: a hinge's angle, a slider's place along it, a distance joint's length (0 for the others).
struct JointState {
    JointId id;
    Joint::Form form = Joint::Form::point;
    BodyId one, other;
    float value = 0;
    Motor::Mode drive = Motor::Mode::off;
    float target = 0;
};

// A walking character (Jolt's CharacterVirtual): a capsule moved by what it is told and what it meets — up steps,
// along slopes it can climb, stuck to the floor going down — not a body: it pushes bodies and is not pushed by
// them, and a ray does not meet it. As data on the world: saved, restored and branched with the bodies.
struct CharacterId {
    std::uint32_t value = 0;
    bool operator==(const CharacterId&) const = default;
    auto operator<=>(const CharacterId&) const = default;
    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
};
struct CharacterDef {
    Vec at;                     // its feet
    float height = 1.8f, radius = 0.3f;
    float steepest = 0.8727f;   // radians: the steepest slope it walks up (50°)
    float step = 0.4f;          // the highest step it walks up
    float stick = 0.5f;         // how far down it stays on the floor walking down
    float mass = 70, strength = 100;  // what it pushes bodies with (kg, N)
};
enum class Ground : std::uint8_t { on_ground, on_steep_ground, not_supported, in_air };
// A character after a step: where its feet are, how it moves, what it stands on.
struct CharacterState {
    CharacterId id;
    Vec at, velocity;
    Ground ground = Ground::in_air;
    BodyId standing_on;  // none: in the air, or on nothing of the world's
    Vec ground_point, ground_normal;
    Vec walk;            // the velocity it is told to walk at
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
    // The first body a shape, turned by `turn`, meets moved from `from` to `to`: where along, where on that body,
    // and the normal there.
    [[nodiscard]] std::optional<RayHit> cast(const Shape& shape, Turn turn, Vec from, Vec to) const;
    // A part of a body whose shape is a compound with movable parts, placed again in the body.
    [[nodiscard]] std::expected<void, Error> move_part(BodyId body, std::size_t part, Vec at, Turn turn);

    // Joints: made between bodies of the world (a body removed takes its joints with it).
    [[nodiscard]] std::expected<JointId, Error> join(const Joint& joint);
    void unjoin(JointId joint) noexcept;
    [[nodiscard]] std::span<const JointId> joints() const noexcept { return joints_; }
    [[nodiscard]] std::optional<JointState> joint(JointId joint) const noexcept;
    // A hinge's or a slider's motor told what to do from the next step.
    [[nodiscard]] std::expected<void, Error> drive(JointId joint, Motor::Mode mode, float target);

    // Characters: told to walk at a velocity (along the ground; held until told otherwise) and to jump (once,
    // when it stands); each step moves them before the bodies.
    [[nodiscard]] std::expected<CharacterId, Error> add(const CharacterDef& character);
    void remove(CharacterId character) noexcept;
    [[nodiscard]] std::span<const CharacterId> characters() const noexcept { return characters_; }
    [[nodiscard]] std::optional<CharacterState> character(CharacterId character) const noexcept;
    void walk(CharacterId character, Vec velocity) noexcept;
    void jump(CharacterId character, float speed) noexcept;
    // Every character after the last step, in the order of their ids: a step's output, as the contacts are.
    [[nodiscard]] std::span<const CharacterState> grounds() const noexcept { return grounds_; }
    // The bodies a shape placed at `at`, turned by `turn`, overlaps, each once, in the order of their ids.
    void overlapping(const Shape& shape, Vec at, Turn turn, std::vector<BodyId>& out) const;

    // The whole state at this step boundary: each body as it was made (its kind, its material, its shape's
    // bytes), each joint and character, and Jolt's state of all of them. Restored into this world, every id names its body again (one
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
    std::vector<JointId> joints_;
    std::vector<CharacterId> characters_;
    std::vector<Contact> contacts_;
    std::vector<CharacterState> grounds_;
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
