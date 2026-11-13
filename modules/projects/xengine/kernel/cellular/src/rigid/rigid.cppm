// xengine.cellular.rigid — rigid bodies made of the lattice's cells (the plan's §6.7, D-133's contract 3): the
// package's feature `rigid`, which joins the lattice of matter (the feature `matter`) and xengine.dynamics' rigid
// family. A body is a grid of cells of solid matter that moves as one; the lattice keeps simulating its cells —
// they burn, heat, get wet — and what is no longer solid leaves it, so a body burns away, is re-made of what is
// left, and falls apart into several. The field acts on bodies by what touches them: a liquid's weight
// (buoyancy, from the pressure of each column of liquid on the faces it wets), drag, a blast's pressure.
//
// A frame, for a host:
//     lattice.step(field, next) …                  // bodies stamped: their cells are cells of the field
//     bodies.push(field);                           // what the field does to them, for the world's next step
//     bodies.lift(field);                           // out of the field, with what the field made of their cells
//     bodies.terrain(field);                        // the field's own solid cells as fixed collision
//     world.step();
//     bodies.stamp(field);                          // back in at their new poses
//
// Coordinates: the field's — x across, y down (row by row), so the rigid world's gravity points to +y — in
// metres, `Options::cell` a cell. Matter is conserved: what a body moves into (a liquid, a powder) changes places
// with the nearest gas, and that gas lies under the body until it moves away. Determinism: the float work (where
// a cell of the field falls in a body's grid) is + − × and floor, no contraction: the same bits everywhere, as
// the rigid world's.
export module xengine.cellular.rigid;
import std;
export import xengine.cellular.matter;
export import xengine.dynamics.rigid;
export import xengine.cellular.particles;

export namespace xengine::cellular::rigid {
namespace dyn = xengine::dynamics::rigid;

struct Options {
    float cell = 0.1f;            // metres a cell
    float gravity = 9.8f;         // what a liquid's column weighs by (the rigid world's gravity, +y)
    std::uint32_t room = 8;       // how far, in cells, what a body moves into looks for a gas to change places with
    float drag = 0.5f;            // a liquid's drag on each face it wets, × its density × the face × the speed
    std::uint32_t terrain_tile = 32;
};
// What the last call of each did.
struct Statistics {
    std::uint64_t covered = 0, displaced = 0, hidden = 0;  // stamp: cells covered; moved to a gas; kept under, no gas near
    std::uint64_t lost = 0, split = 0, gone = 0;            // lift: cells no longer solid; bodies made by a split; bodies with no cell left
    std::uint64_t wetted = 0;                               // push: faces of a liquid
    std::uint64_t terrain_tiles = 0;                        // terrain: tiles rebuilt
};
// What the coupling did to bodies in a lift, said to whoever asks (Bodies::log): a world's journal (D-169).
struct BodyLog {
    struct Left {
        dyn::BodyId body;
        std::uint32_t cell = 0;  // the field's cell that is no longer solid, and so no longer the body's
    };
    struct Split {
        dyn::BodyId of, made;    // a piece that came apart from `of` is the body `made`
    };
    std::vector<Left> left;
    std::vector<Split> split;
    std::vector<dyn::BodyId> gone;  // bodies with no cell left
};
// A body as the coupling sees it.
struct View {
    dyn::BodyId body;
    std::uint32_t cells = 0;       // solid cells it is made of
    std::uint32_t width = 0, height = 0;
};

class Bodies {
public:
    [[nodiscard]] static std::expected<Bodies, std::string> make(const xengine::matter::Program& program, Shape field, dyn::World& world, const Options& options = {});

    // The solid cells of `region` become moving bodies, one a connected piece (four neighbours). They stay in the
    // field (stamped where they are); what is left where one moves away is `fill` (one instance's channels: the
    // room's air) until the field has made its own gas there.
    [[nodiscard]] std::expected<std::vector<dyn::BodyId>, std::string> carve(std::span<std::uint32_t> field, Tile region, std::span<const std::uint32_t> fill);
    void push(std::span<const std::uint32_t> field);
    void lift(std::span<std::uint32_t> field);
    void terrain(std::span<const std::uint32_t> field);
    void stamp(std::span<std::uint32_t> field);

    [[nodiscard]] std::vector<View> bodies() const;
    [[nodiscard]] std::optional<View> body(dyn::BodyId body) const;
    // Where the bodies' stamped cells are now, each grown by `room` (what a stamp moves aside): the field's cells
    // written from outside a lattice step — a sparse host wakes their tiles.
    [[nodiscard]] std::vector<Tile> touched() const;
    void touched(std::vector<Tile>& out) const;  // the same into `out` (cleared first): no allocation once it has room
    // Which body holds the field's cell now (stamped), if any.
    [[nodiscard]] std::optional<dyn::BodyId> owner(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] const Statistics& last() const noexcept { return last_; }
    // Who is told what a lift does to the bodies from now on (none: no one).
    void log(BodyLog* log) noexcept { log_ = log; }
    // What the coupling holds, as bytes (with the rigid world's own save, a whole state): the bodies' cells,
    // where each was stamped and what lies under, the terrain's tiles. Restored onto a rigid world restored or
    // made from the same save — whose ids name the same bodies.
    void save(std::vector<std::uint8_t>& out) const;
    [[nodiscard]] std::expected<void, std::string> restore(std::span<const std::uint8_t> saved, std::size_t& at);
    // The same coupling for another rigid world made from the same save (a branch).
    [[nodiscard]] Bodies onto(dyn::World& world) const;

private:
    struct Piece {
        dyn::BodyId body;
        std::int32_t left = 0, top = 0;          // the grid's corner, in cells from the body's origin
        std::uint32_t width = 0, height = 0;
        std::vector<std::uint32_t> cells;        // [channel][local]; channel 0 `none` where empty
        std::vector<std::uint32_t> covered;      // field cells the last stamp covered
        std::vector<std::uint32_t> from;         // the local cell each came from
        std::vector<std::uint32_t> under;        // [covered][channel]: what lies under each
    };
    Bodies() = default;
    [[nodiscard]] bool solid(std::uint32_t material) const noexcept;
    [[nodiscard]] bool gas(std::uint32_t material) const noexcept;
    [[nodiscard]] bool liquid(std::uint32_t material) const noexcept;
    [[nodiscard]] std::expected<void, std::string> shape(Piece& piece, bool make_body, const dyn::Pose* like);
    void reshape(std::size_t index);

    const xengine::matter::Program* program_ = nullptr;
    dyn::World* world_ = nullptr;
    Options options_;
    Shape field_;
    std::uint32_t channels_ = 0, density_ = 0, friction_ = 0, restitution_ = 0;
    struct Push { std::uint32_t channel = 0; float force = 0; std::optional<std::uint32_t> scale; };
    std::vector<Push> pushes_;  // the definition's laws of motion down a slope that give a force
    std::vector<Piece> pieces_;
    std::vector<std::uint32_t> owner_;           // a cell: 0, or the piece's index + 1
    std::vector<std::uint32_t> fill_;
    std::vector<dyn::BodyId> terrain_;           // a fixed body a tile (none: no solid there)
    std::vector<std::uint64_t> terrain_hash_;
    Statistics last_;
    std::vector<std::uint8_t> read_;             // lift's scratch
    // What re-making a body's or a terrain tile's shapes works in, kept between frames: a frame allocates nothing
    // of ours where the terrain changes (what the rigid world itself makes for a new body is its own).
    struct Scratch;
    std::shared_ptr<Scratch> scratch_;
    BodyLog* log_ = nullptr;
};

// A world of matter with bodies in it: the lattice, the rigid world and the coupling, stepped in their order and
// saved, restored and branched as one (world.branch). Made in place (the coupling holds the rigid world).
struct WorldOptions {
    Shape shape;
    Shape tile{16, 16};
    std::uint32_t seed = 0;         // of the lattice's sideways choices
    std::string fill;               // the material the world is made of at first, and what lies beyond its edges (a gas)
    bool open = true;               // its edges a bath of `fill` (heat leaves, a fire there has its oxygen); else nothing
    bool sparse = true;             // the lattice runs the tiles awake
    // m/s², toward +y. Unset: the definition's law of motion toward a direction (none: 0) — a lattice's cells
    // fall along +y, so a law toward anything else is refused.
    std::optional<float> gravity;
    Options bodies;
    particles::Options flying;  // its cell and gravity are the world's
};
// What a frame did besides the lattice's laws, said by the world itself (the foundation plan's §6.10.1): cells
// thrown and landed (particles::Log), cells that left a body, bodies split and gone (BodyLog), bodies carried,
// and what was done to the world from outside since the frame before. Kept only while the world is watched, and
// no part of its state: not hashed, not saved, not branched — a world watched is the same bits as one that is not.
// What a record of causes is made from, beside the laws judged again on `before()` and `lattice`.
struct Journal : particles::Log, BodyLog {
    using Cause = particles::Cause;
    struct Carried {
        dyn::BodyId body;
        particles::Vec from, to;  // its origin before and after the rigid step, in metres
        float turned = 0;         // radians
    };
    enum class EditKind : std::uint8_t { place, set, blast, carve };
    struct Edit {
        EditKind kind = EditKind::place;
        Tile region;                        // place, set, carve; blast: the square about it
        std::uint32_t channel = 0, value = 0;  // place: `value` the material; set: the channel and its value
        particles::Vec centre;              // blast
        float radius = 0, speed = 0;
        bool breaks = false;
        std::uint32_t by = 0;               // what MatterWorld::by said then: who made it, to whoever keeps that
    };
    std::uint64_t frame = 0;                // the frame it is of: frames() after the step
    std::vector<Edit> edits;                // made since the step before, in order; what they threw is in `thrown`
    std::vector<Carried> carried;           // bodies whose pose the rigid step changed
    std::vector<std::uint32_t> lattice;     // the cells as the lattice's step left them, before anything else wrote
    std::vector<std::uint8_t> activity;     // the tiles' bytes the step began with (a tile asleep then was not run)
    void clear();
};
class MatterWorld {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<MatterWorld>, std::string> make(const xengine::matter::Program& program, const WorldOptions& options);
    MatterWorld(const MatterWorld&) = delete;
    MatterWorld& operator=(const MatterWorld&) = delete;

    [[nodiscard]] const xengine::matter::Program& program() const noexcept { return *program_; }
    [[nodiscard]] Shape shape() const noexcept { return options_.shape; }
    [[nodiscard]] std::span<const std::uint32_t> cells() const noexcept { return cells_; }
    [[nodiscard]] dyn::World& rigid() noexcept { return *rigid_; }
    [[nodiscard]] Bodies& bodies() noexcept { return *bodies_; }
    [[nodiscard]] particles::Particles& flying() noexcept { return *flying_; }
    // The same to read: what a presenter is given (D-178: it shows the world, and cannot change it).
    [[nodiscard]] const dyn::World& rigid() const noexcept { return *rigid_; }
    [[nodiscard]] const Bodies& bodies() const noexcept { return *bodies_; }
    [[nodiscard]] const particles::Particles& flying() const noexcept { return *flying_; }
    // A blast at `centre` (cells): what is not gas within `radius` thrown outward (solids too when `breaks`);
    // bodies are pushed by it at the next frame through the field's pressure, if the world has one.
    std::uint32_t blast(particles::Vec centre, float radius, float speed, bool breaks = false);
    [[nodiscard]] std::uint64_t frames() const noexcept { return tick_; }
    // Watched: each step keeps its journal. Off (as made, and in every branch): nothing is kept and a frame costs
    // what it did. Either way the world is the same bits.
    void watch(bool on);
    [[nodiscard]] bool watched() const noexcept { return watched_; }
    [[nodiscard]] const Journal& journal() const noexcept { return journal_; }
    // The cells the last step's lattice began with (its input): with `journal().lattice`, what the laws did.
    [[nodiscard]] std::span<const std::uint32_t> before() const noexcept { return next_; }
    // A number the journal's edits carry from now on: who is making them, to whoever keeps who that is. As the
    // journal, no part of the world's state.
    void by(std::uint32_t who) noexcept { by_ = who; }
    // The lattice the cells run on, as a step runs it — what judges a step again needs.
    [[nodiscard]] const cellular::Program& lattice() const noexcept { return lattice_; }
    [[nodiscard]] Boundary boundary() const noexcept { return options_.open ? open_to(outside_) : nothing_beyond; }
    [[nodiscard]] Shape tile() const noexcept { return options_.tile; }
    [[nodiscard]] std::uint32_t seed() const noexcept { return options_.seed; }
    [[nodiscard]] float cell() const noexcept { return options_.bodies.cell; }  // metres a cell
    [[nodiscard]] std::span<const std::uint8_t> activity() const noexcept { return activity_; }

    // Cells written from outside a frame (the tiles they are in wake).
    void place(Tile region, std::uint32_t material);
    void set(std::uint32_t channel, Tile region, std::uint32_t value);
    // The solid cells of `region` become bodies.
    [[nodiscard]] std::expected<std::vector<dyn::BodyId>, std::string> carve(Tile region);
    // One frame: the lattice's step, what the field does to the bodies, the bodies out, the terrain, the rigid
    // step, the bodies back in.
    void step();

    [[nodiscard]] std::vector<std::uint8_t> save() const;
    [[nodiscard]] std::expected<void, std::string> restore(std::span<const std::uint8_t> saved);
    [[nodiscard]] std::expected<std::unique_ptr<MatterWorld>, std::string> branch() const;
    // The field's every value and the rigid world's every body, bit for bit.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

private:
    MatterWorld() = default;
    const xengine::matter::Program* program_ = nullptr;
    WorldOptions options_;
    cellular::Program lattice_;
    std::vector<std::uint32_t> cells_, next_, outside_;
    std::vector<std::uint8_t> activity_;
    std::vector<Tile> touched_;  // a frame's scratch
    std::uint64_t tick_ = 0;
    std::unique_ptr<dyn::World> rigid_;
    std::unique_ptr<Bodies> bodies_;
    std::unique_ptr<particles::Particles> flying_;
    bool watched_ = false;
    std::uint32_t by_ = 0;
    Journal journal_, pending_;  // the last step's; what was done since (edits and what they threw)
    std::vector<std::pair<dyn::BodyId, dyn::Pose>> poses_;  // a watched frame's scratch
};

// A world of matter as a value: a copy is a branch (the whole world, bit for bit), so a timeline keeps it and
// a game's state holds it — mlibs.simulation's Model, xengine.runtime's Playable — with no aliasing between copies.
class WorldValue {
public:
    WorldValue() noexcept;
    explicit WorldValue(std::unique_ptr<MatterWorld> world) noexcept;
    WorldValue(const WorldValue& other);
    WorldValue& operator=(const WorldValue& other);
    WorldValue(WorldValue&&) noexcept;
    WorldValue& operator=(WorldValue&&) noexcept;
    ~WorldValue();
    // Empty: made so, or a copy whose branch failed (Box2D made no world of its own snapshot).
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] MatterWorld& operator*() const noexcept;
    [[nodiscard]] MatterWorld* operator->() const noexcept;

private:
    std::unique_ptr<MatterWorld> world_;
};

}  // namespace xengine::cellular::rigid
