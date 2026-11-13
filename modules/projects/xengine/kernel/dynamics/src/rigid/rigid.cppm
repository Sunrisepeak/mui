// xengine.dynamics.rigid — the 2D rigid family (P1; D-170, D-176): Box2D v3 (MIT, through mwrap's
// mcpplibs.mwrap.box2d) under this package's contract. Box2D solves; what is ours is the shape of the API
// every family shares — numbers in (a material's mechanical columns, not the table), a fixed step, ids that
// stay the same across a restore and a branch, the state as bytes, its hash — and the record of what touched
// what. The feature `rigid`: a consumer that does not take it links no Box2D.
//
//     namespace rigid = xengine::dynamics::rigid;
//     auto world = rigid::World::make({.gravity = {0, -10}});
//     const auto crate = world->add({.kind = rigid::Kind::moving, .at = {0, 4}}, rigid::Shape::box(0.5, 0.5), {.density = 1});
//     world->step();                                   // 1/60 s, 4 substeps by default
//     const auto saved = world->save();                // a step boundary: the whole state, this version's bytes
//     auto branch = world->branch();                   // a world of its own from here (world.branch)
//     world->restore(saved);                           // back: every id still names its body
//
// Determinism: Box2D is cross-platform deterministic built as it is here (no contraction, no fast math, its
// own trigonometry) — the same bits on x86-64, arm64 and wasm32, with any number of workers. A saved state is
// Box2D's snapshot image: valid for the same Box2D version only (its format is experimental upstream), so it
// is what a rollback, an undo and a branch use within a run; what is kept across versions is the
// application's own state.
export module xengine.dynamics.rigid;
import std;

export namespace xengine::dynamics::rigid {

struct Vec { float x = 0, y = 0; };
enum class Kind : std::uint8_t { fixed, moving, kinematic };
// A body's mechanical numbers — what xengine.dynamics.matter's params_of reads from a material's row.
struct Material {
    float density = 1.0f;      // kg per m²
    float friction = 0.6f;
    float restitution = 0.0f;
};
struct Shape {
    enum class Form : std::uint8_t { box, circle, polygon, segment } form = Form::box;
    Vec half{0.5f, 0.5f};      // box: half extents
    Vec centre;                // box, circle: where in the body
    float radius = 0.5f;       // circle; polygon: rounding
    float angle = 0;           // box: turned in the body
    std::vector<Vec> points;   // polygon (convex, at most 8), segment (2)
    [[nodiscard]] static Shape box(float half_width, float half_height) { return {.form = Form::box, .half = {half_width, half_height}}; }
    [[nodiscard]] static Shape circle(float radius) { return {.form = Form::circle, .radius = radius}; }
    [[nodiscard]] static Shape polygon(std::vector<Vec> points, float radius = 0) { return {.form = Form::polygon, .radius = radius, .points = std::move(points)}; }
    [[nodiscard]] static Shape segment(Vec from, Vec to) { return {.form = Form::segment, .points = {from, to}}; }
};
struct BodyDef {
    Kind kind = Kind::moving;
    Vec at;
    float angle = 0;
    Vec velocity;
    float spin = 0;
    bool bullet = false;       // continuous collision against moving bodies too
    bool fixed_rotation = false;
};
// A body, the same in every world branched from the one it was made in.
struct BodyId {
    std::int32_t index = 0;    // 0: none
    std::uint16_t generation = 0;
    bool operator==(const BodyId&) const = default;
    [[nodiscard]] explicit operator bool() const noexcept { return index != 0; }
};
struct Pose { Vec at; float angle = 0; Vec velocity; float spin = 0; bool awake = true; };
// Two bodies that began or ended touching in the last step, where, and how hard they met.
struct Contact {
    BodyId one, other;
    bool began = true;
    Vec point, normal;
    float approach = 0;        // speed along the normal as they met (began only)
};
struct Options {
    Vec gravity{0, -10};
    float step = 1.0f / 60.0f;
    int substeps = 4;
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
    // Another shape on a body.
    [[nodiscard]] std::expected<void, Error> attach(BodyId body, const Shape& shape, const Material& material = {});
    void remove(BodyId body) noexcept;
    [[nodiscard]] bool has(BodyId body) const noexcept;
    [[nodiscard]] std::optional<Pose> pose(BodyId body) const noexcept;
    [[nodiscard]] std::vector<BodyId> bodies() const;
    void push(BodyId body, Vec impulse, std::optional<Vec> at = std::nullopt) noexcept;  // at: the centre
    void set_velocity(BodyId body, Vec velocity, float spin) noexcept;

    // One fixed step (Options::step, its substeps); what began and ended touching in it.
    void step() noexcept;
    [[nodiscard]] std::span<const Contact> contacts() const noexcept { return contacts_; }
    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }

    // The whole state at this step boundary, as Box2D's snapshot image with the step count: restore it into
    // this world (every id still names its body), or make a world of its own from it.
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
    std::vector<BodyId> bodies_;  // in the order made: what a hash and a save go through
    std::vector<Contact> contacts_;
    std::uint64_t steps_ = 0;
    Options options_;
};

}  // namespace xengine::dynamics::rigid
