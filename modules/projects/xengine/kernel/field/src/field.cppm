// xengine.field — the air and what moves it, as one more carrier of xengine.matter (the M6.5 design's T5.2; the
// foundation plan's §6.4: the field carrier). A sparse lattice of coarse cells, allocated in blocks only where
// something is: each cell is an instance of matter (the air, at its temperature, wetness, charge), and the
// standard laws run between neighbouring cells exactly as they run on a fine lattice, on bodies and on particles —
// heat flows, wetness seeps, charge flows. What a carrier does itself is move things (D-181: physics moves, the
// laws say what): here, the wind. Warm air rises (buoyancy from how much warmer a cell is than the air about), the
// wind relaxes toward the far wind, it carries itself and the heat along (semi-Lagrangian advection, Stam 1999),
// and a fixed number of pressure iterations keeps it from piling up. So a fire under it makes an updraft, and a
// wind blows the heat of a fire downwind — no rule here names a fire or a wind's effect.
//
//     auto air = field::Field::make(program, {.air = *program.material("air"), .wind = {2, 0, 0}}).value();
//     air.cover({0, 2, 0}, 6);                                   // cells about a place (blocks of 8³)
//     air.set(*air.cell_at({0, 0.5f, 0}), temperature, hot);     // a quantity applied
//     air.step(guests);                                          // the laws across cells and the guests in them, then the wind
//     air.wind_at({0, 3, 0});                                    // what a body or a particle there is pushed by
//
// Other carriers join a step as guests (bodies: xengine.dynamics.objects; particles): each guest is paired with
// the cell it is in, so what is about a body is the air where it is, not a bath. Determinism: the cells in the
// order their blocks were made, the pairs in a fixed order, the wind's float work with + − × ÷ and floor and no
// contraction, the matter step the kernel's reference — the same bits everywhere.
export module xengine.field;

import std;
export import xengine.matter;
export import mlibs.math;

export namespace xengine::field {

namespace matter = xengine::matter;
using mlibs::math::Vec3;

inline constexpr int block_side = 8;  // cells along each side of a block
inline constexpr std::size_t block_cells = static_cast<std::size_t>(block_side) * block_side * block_side;

struct FieldOptions {
    float              cell = 1;            // m across a cell
    matter::MaterialId air = matter::none;  // what a cell is made of when it is made
    float              air_kelvin = 293;    // how warm the air about is (and a new cell)
    Vec3               wind{0, 0, 0};       // m/s: the wind far from anything
    float              buoyancy = 0.0334f;  // m/s² a kelvin warmer than `air_kelvin` (g ÷ 293 K: Boussinesq)
    float              relax = 0.5f;        // a second's share by which the wind returns to the far wind
    std::uint32_t      projection = 20;     // pressure iterations a step (fixed: the same work every step)
    float              step = 1.0f / 60;    // s
};

// Instances of another carrier taking part in a field's step: their state channel-major ([channel][guest], read
// and written), the pairs among them (by guest), and where each is (paired with the cell there, weight `weight`).
struct Guests {
    std::span<std::uint32_t>    state;
    std::span<const matter::Pair> pairs;
    std::span<const Vec3>       at;
    std::uint32_t               weight = 1;
    std::span<const std::uint32_t> capacity;  // how much each holds (none: 1 each), as matter's Carrier says
    std::span<const std::uint32_t> exposed;   // how much of each its reactions run over (none: all it holds)
};

// A parked block as a value: where it is, its coarse value of each channel, each cell's difference from it, its
// wind. What a region is to save, send and put back — the same bits read as written (`bytes`, `read`: "OMFR",
// version 1, little-endian).
struct Region {
    std::array<std::int32_t, 3> block{0, 0, 0};    // in blocks of 8³ cells
    std::vector<std::uint32_t>  coarse;            // a value each channel
    std::vector<std::int64_t>   difference;        // [channel][cell of the block]
    Vec3                        wind{0, 0, 0};

    [[nodiscard]] std::vector<std::uint8_t> bytes() const;
    [[nodiscard]] static std::expected<Region, std::string> read(std::span<const std::uint8_t> bytes);
    // Written out (a defaulted == here crashes GCC 16.1 importing the module): every part, the wind by its bits.
    [[nodiscard]] bool operator==(const Region& other) const noexcept {
        const auto bits = [](Vec3 at) { return std::array{std::bit_cast<std::uint32_t>(at.x), std::bit_cast<std::uint32_t>(at.y), std::bit_cast<std::uint32_t>(at.z)}; };
        return block == other.block && coarse == other.coarse && difference == other.difference && bits(wind) == bits(other.wind);
    }
};

// A field is a value: a copy is a branch that goes its own way from here (the program shared, read only).
class Field {
public:
    [[nodiscard]] static std::expected<Field, std::string> make(const matter::Program& program, FieldOptions options);

    // Cells about a sphere (whole blocks, so a little more): made of the air, still, at the air's temperature.
    void cover(Vec3 at, float radius);
    [[nodiscard]] std::size_t cells() const noexcept { return blocks_.size() * block_cells; }
    [[nodiscard]] std::optional<std::uint32_t> cell_at(Vec3 at) const noexcept;
    [[nodiscard]] Vec3 centre(std::uint32_t cell) const noexcept;

    // A channel of a cell, as stored; a quantity applied.
    [[nodiscard]] std::optional<std::uint32_t> get(std::uint32_t cell, std::uint32_t channel) const noexcept;
    bool set(std::uint32_t cell, std::uint32_t channel, std::uint32_t value) noexcept;
    // The wind at a place (between cells, the blend of the eight about it; where there are none, the far wind).
    [[nodiscard]] Vec3 wind_at(Vec3 at) const noexcept;
    // How warm the air is at a place, in kelvin (where there are no cells, the air about).
    [[nodiscard]] float kelvin_at(Vec3 at) const noexcept;

    // ── regions (streaming) ──
    // A block far from anything that matters, parked: its cells become one coarse instance holding their mean
    // (capacity 512: it changes as slowly as the whole block would), still under the laws with its neighbours —
    // the cells beside it and other parked blocks — and each cell's difference from the mean kept, exactly. A
    // parked block expanded again is each cell's kept difference over the coarse value now: the block's total
    // (512 × coarse + the differences) the same before and after, bit for bit. The wind is not solved in it (the
    // cells beside it see the far wind there). False: no such block, or already so.
    bool park(Vec3 at);
    bool expand(Vec3 at);
    // Streaming: blocks with any part within `near` of `centre` expanded (made where none was), blocks wholly
    // beyond `far` parked; between, as they are (so a walker on a border does not flicker).
    void follow(Vec3 centre, float near, float far);
    [[nodiscard]] std::size_t parked() const noexcept { return coarse_keys_.size(); }
    // A region out of the field (parked first if it is expanded): it and its totals leave with it, the cells beside
    // it now at the field's edge. None: nothing there.
    [[nodiscard]] std::optional<Region> take(Vec3 at);
    // A region put back, parked, where it was: false if a block is there or it is not of this program's channels.
    bool put(Region region);
    // A channel's total over the field — each cell, and each parked block as 512 × its coarse value plus its
    // cells' kept differences: what a switch between the two keeps exactly.
    [[nodiscard]] std::int64_t total(std::uint32_t channel) const noexcept;

    // One tick: the laws across the cells and the guests in them, then the wind moved and the heat carried.
    [[nodiscard]] std::expected<matter::Report, matter::Failure> step(std::span<const Guests> guests = {});
    // What happened to an instance of the last step (cells first, then each group of guests in turn).
    [[nodiscard]] std::span<const matter::Event> events() const noexcept { return events_; }
    // Why an instance of the last step is what it is (cells first, then the guests in the order given).
    [[nodiscard]] std::optional<matter::Why> why(std::uint32_t instance) const;
    // Where the guests of the last step began (group `group`'s first instance).
    [[nodiscard]] std::uint32_t first_guest(std::size_t group) const noexcept { return group < firsts_.size() ? firsts_[group] : 0; }

    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] const FieldOptions& options() const noexcept { return options_; }
    [[nodiscard]] const matter::Program& program() const noexcept { return *program_; }

private:
    using Key = std::array<std::int32_t, 3>;
    [[nodiscard]] std::optional<std::uint32_t> cell_of(std::int64_t x, std::int64_t y, std::int64_t z) const noexcept;
    [[nodiscard]] std::array<std::int64_t, 3> place_of(std::uint32_t cell) const noexcept;
    void link();  // the pairs between neighbouring cells, again
    void move_wind();
    template <class Read>
    [[nodiscard]] double sample(Vec3 at, double outside, Read read) const noexcept;

    const matter::Program*     program_ = nullptr;
    FieldOptions               options_;
    std::uint32_t              temperature_ = 0;
    std::uint32_t              air_stored_ = 0;   // the air's temperature, as stored
    std::map<Key, std::uint32_t> index_;          // a block's place → its number
    std::vector<Key>           blocks_;           // in the order made
    std::vector<std::uint32_t> state_;            // the cells', channel-major
    std::vector<float>         wind_x_, wind_y_, wind_z_, scratch_x_, scratch_y_, scratch_z_, pressure_, pressure_next_, divergence_;
    std::vector<std::uint32_t> heat_;             // the temperature carried, a step's scratch
    std::vector<matter::Pair>  links_;            // between neighbouring cells, and cells beside a parked block with its coarse instance
    // Parked blocks: each a coarse instance (after the cells in a step's numbering) and its cells' differences.
    struct Parked {
        std::uint32_t             coarse = 0;
        std::vector<std::int64_t> difference;  // [channel][cell of the block]
        Vec3                      wind{0, 0, 0};
    };
    std::map<Key, Parked>      parked_;
    std::vector<Key>           coarse_keys_;      // a coarse instance each, in the order parked
    std::vector<std::uint32_t> coarse_state_;     // channel-major, a coarse instance each
    std::vector<matter::Pair>  coarse_links_;     // between parked blocks beside each other (coarse numbers)
    [[nodiscard]] std::optional<std::uint32_t> coarse_at(Vec3 at) const noexcept;
    void drop_block(std::uint32_t block);         // a block's cells out of the arrays (its number given to the last)
    static constexpr std::uint32_t nobody = 0xffffffffu;
    std::vector<std::array<std::uint32_t, 6>> near_;  // each cell's neighbours: +x, −x, +y, −y, +z, −z (nobody: none)
    std::vector<matter::Pair>  pairs_;            // a step's: links, then guests
    std::vector<std::uint32_t> capacity_, exposed_;  // a step's: each instance's
    std::vector<std::uint32_t> input_, output_, before_;
    std::vector<std::uint32_t> firsts_;
    std::vector<matter::Event> events_;
    matter::Workspace          workspace_;
};

}  // namespace xengine::field
