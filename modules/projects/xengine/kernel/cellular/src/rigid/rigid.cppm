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

export namespace xengine::cellular::rigid {
namespace dyn = xengine::dynamics::rigid;

struct Options {
    float cell = 0.1f;            // metres a cell
    float gravity = 9.8f;         // what a liquid's column weighs by (the rigid world's gravity, +y)
    std::uint32_t room = 8;       // how far, in cells, what a body moves into looks for a gas to change places with
    float drag = 0.5f;            // a liquid's drag on each face it wets, × its density × the face × the speed
    float blast = 0.0f;           // newtons a face per unit of the `pressure` channel (0: none)
    std::uint32_t terrain_tile = 32;
};
// What the last call of each did.
struct Statistics {
    std::uint64_t covered = 0, displaced = 0, hidden = 0;  // stamp: cells covered; moved to a gas; kept under, no gas near
    std::uint64_t lost = 0, split = 0, gone = 0;            // lift: cells no longer solid; bodies made by a split; bodies with no cell left
    std::uint64_t wetted = 0;                               // push: faces of a liquid
    std::uint64_t terrain_tiles = 0;                        // terrain: tiles rebuilt
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
    // Which body holds the field's cell now (stamped), if any.
    [[nodiscard]] std::optional<dyn::BodyId> owner(std::uint32_t x, std::uint32_t y) const;
    [[nodiscard]] const Statistics& last() const noexcept { return last_; }
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
    std::optional<std::uint32_t> pressure_;
    std::vector<Piece> pieces_;
    std::vector<std::uint32_t> owner_;           // a cell: 0, or the piece's index + 1
    std::vector<std::uint32_t> fill_;
    std::vector<dyn::BodyId> terrain_;           // a fixed body a tile (none: no solid there)
    std::vector<std::uint64_t> terrain_hash_;
    Statistics last_;
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
    float gravity = 9.8f;
    Options bodies;
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
    [[nodiscard]] std::uint64_t frames() const noexcept { return tick_; }

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
    std::uint64_t tick_ = 0;
    std::unique_ptr<dyn::World> rigid_;
    std::unique_ptr<Bodies> bodies_;
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
