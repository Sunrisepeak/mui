// xengine.cellular.decor — matter that is only for show (D-178): the package's feature `decor`. A world of
// matter is authoritative: it is saved, hashed, replayed and branched, and is the same bits on every machine.
// A decorative layer is a second field beside it, stepped by the same laws from the same program, that READS
// the authoritative field and is never read by it — scenery that catches from a real fire and burns, its heat
// and smoke about it — so a machine with room draws a richer world and the game played is the same one.
//
//     auto decor = cellular::decor::Decor::make(program, shape, {.fill = "air"});
//     decor->place({30, 40, 6, 2}, *program.material("wood"));     // scenery: here only
//     decor->step(world.cells());                                  // each frame it is shown at this step of the ladder
//     presenter.present(decor->cells());                           // instead of the world's cells
//
// The line is one way, and the types say so: a step takes the authoritative cells as `span<const>`, and nothing
// here hands out a way to write them; a game's state holds no Decor (it is the presenting side's, with the
// presenter). What the layer holds is not saved and not hashed, and a machine that does not run it loses nothing
// of the game.
//
// How the two agree: a cell the layer does not own follows the authoritative field wherever that changed since
// the last step, and otherwise goes on by the laws from what the layer made of it (the warmth and the smoke of
// scenery alight, drifting through air the world leaves alone). A cell the layer owns — scenery placed here,
// while it is still solid and the world has not put something of its own there — is the layer's alone.
// Determinism: the lattice's own, from the layer's seed and the authoritative cells it was given; the same
// binary and the same frames give the same bits. Across machines it promises nothing: it is not the game.
export module xengine.cellular.decor;
import std;
export import xengine.cellular.matter;

export namespace xengine::cellular::decor {

struct Options {
    Shape tile{16, 16};
    std::uint32_t seed = 0;   // of the layer's own sideways choices
    std::string fill;         // what lies beyond the edges (a gas): the world's own fill
    bool open = true;         // its edges a bath of `fill`; else nothing
    bool sparse = true;
};
// What the last step did.
struct Statistics {
    std::uint64_t followed = 0;   // cells set from the authoritative field (it changed there)
    std::uint64_t owned = 0;      // cells of scenery
    std::uint64_t released = 0;   // scenery that was no longer solid, or that the world put something over
    std::uint64_t differing = 0;  // cells whose material is not the authoritative field's (counted when asked: `count`)
};

class Decor {
public:
    [[nodiscard]] static std::expected<Decor, std::string> make(const xengine::matter::Program& program, Shape shape, const Options& options = {});

    // Scenery: the cells of `region` that are gas in the layer become `material` (a solid), the layer's own.
    // How many. 0 for a material that is not solid, or before the first step (the layer has no field yet).
    std::uint32_t place(Tile region, std::uint32_t material);
    // A frame: the authoritative cells read, the layer stepped once by the laws.
    void step(std::span<const std::uint32_t> authoritative);
    // The layer anew from the authoritative cells: what it made is gone, its scenery placed again as it first
    // was, and `frames` steps run on the same cells so that it does not begin from nothing (after a seek, a
    // branch, a load: the layer is not saved).
    void restart(std::span<const std::uint32_t> authoritative, int frames = 0);

    [[nodiscard]] Shape shape() const noexcept { return shape_; }
    // What a presenter draws: the authoritative field with the layer's own over it. Empty before the first step.
    [[nodiscard]] std::span<const std::uint32_t> cells() const noexcept { return cells_; }
    [[nodiscard]] bool owns(std::uint32_t x, std::uint32_t y) const noexcept;
    // The last step's numbers; `count`: with `differing`, against the cells that step was given.
    [[nodiscard]] Statistics last(std::span<const std::uint32_t> count = {}) const noexcept;

private:
    Decor() = default;
    struct Placed { Tile region; std::uint32_t material = 0; };
    std::uint32_t put(Tile region, std::uint32_t material);

    const xengine::matter::Program* program_ = nullptr;
    Options options_;
    Shape shape_;
    cellular::Program lattice_;
    std::uint32_t channels_ = 0;
    std::vector<std::uint32_t> cells_, next_, seen_, outside_;  // seen: the authoritative cells of the last step
    std::vector<std::uint8_t> owned_, activity_, dirty_;        // a cell; a tile; a tile
    std::vector<Placed> placed_;
    std::uint64_t tick_ = 0;
    Statistics last_;
};

}  // namespace xengine::cellular::decor
