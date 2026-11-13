// xengine.cellular.particles — cells of the lattice in flight (the plan's §6.7, lattice ↔ particles): the
// package's feature `particles`. A cell thrown out of the field — blown by a pressure, knocked by a blast —
// flies as a particle carrying its whole state (every channel), falls under gravity, and is a cell again in
// the last gas it passed before it met anything that is not gas. Nothing is made or lost: a cell thrown leaves
// a cell of the fill (a gas) where it was, and one that lands takes the place of a gas.
//
//     auto flying = ca::particles::Particles::make(program, shape, {.cell = 0.1f});
//     flying->scatter(cells, fill);          // liquids and powders in a pressure above `throw_at`, thrown down its slope
//     flying->blast(cells, fill, {x, y}, 6, 40);   // or a blast: every cell that is not solid within 6 cells, outward
//     flying->step(cells);                   // each frame: fly, and land
//
// A material is what the table says: a gas never flies (it is what they fly through), a solid stays (a body's,
// or the terrain's — unless a blast is told to break solids). Determinism: + − × and floor, no contraction, a
// particle's path walked cell by cell in a fixed order; two runs are the same bits.
export module xengine.cellular.particles;
import std;
export import xengine.cellular.matter;

export namespace xengine::cellular::particles {

struct Vec { float x = 0, y = 0; };
struct Options {
    float cell = 0.1f;           // metres a cell
    float gravity = 9.8f;        // m/s², toward +y (the field's rows go down)
    float step = 1.0f / 60.0f;   // seconds a frame
    float drag = 0.02f;          // of its speed a particle loses a frame to the air
    std::uint32_t most = 65536;  // particles in flight at once; past it, what would be thrown stays
    float throw_at = 0;          // scatter: the pressure (the channel's raw value) a cell is thrown from; 0: never
    float push = 0.003f;         // scatter: m/s per unit of the pressure's difference across the cell
    float fastest = 15.0f;       // m/s: no cell is thrown faster
};
// The last step's: what was thrown since the step before (and refused, no room), what landed, what flies on.
struct Statistics { std::uint64_t thrown = 0, landed = 0, flying = 0, refused = 0; };

class Particles {
public:
    [[nodiscard]] static std::expected<Particles, std::string> make(const xengine::matter::Program& program, Shape field, const Options& options = {});

    // A cell thrown with a velocity (m/s): out of the field into flight; `fill` (one instance: a gas) left where
    // it was. False for a gas, a cell out of the field, or no room.
    bool fling(std::span<std::uint32_t> field, std::uint32_t x, std::uint32_t y, Vec velocity, std::span<const std::uint32_t> fill);
    // Every liquid or powder in a pressure of at least `throw_at`, thrown down the pressure's slope.
    std::uint32_t scatter(std::span<std::uint32_t> field, std::span<const std::uint32_t> fill);
    // A blast at `centre` (cells): every cell within `radius` that is not gas — and not solid unless `breaks` —
    // thrown outward, the faster the nearer (`speed` m/s at the centre).
    std::uint32_t blast(std::span<std::uint32_t> field, std::span<const std::uint32_t> fill, Vec centre, float radius, float speed, bool breaks = false);
    // A frame: each particle falls and moves, walking the cells it passes; it lands in the last gas it passed
    // before one that is not, or where it is when it has slowed to nothing on something.
    void step(std::span<std::uint32_t> field);

    [[nodiscard]] std::size_t flying() const noexcept { return position_.size(); }
    [[nodiscard]] std::span<const Vec> positions() const noexcept { return position_; }   // in cells
    // Every particle's channels, particle after particle; and their centres as x, y pairs (in cells) — what a
    // presenter draws them from (cellular.present's draw_loose).
    [[nodiscard]] std::span<const std::uint32_t> values() const noexcept { return cells_; }
    void centres(std::vector<float>& out) const;
    // A particle's channel (its material is channel 0).
    [[nodiscard]] std::uint32_t value(std::size_t particle, std::uint32_t channel) const noexcept { return cells_[particle * channels_ + channel]; }
    [[nodiscard]] const Statistics& last() const noexcept { return last_; }
    // What is in flight, as bytes (a world's save), and back.
    void save(std::vector<std::uint8_t>& out) const;
    [[nodiscard]] std::expected<void, std::string> restore(std::span<const std::uint8_t> saved, std::size_t& at);

private:
    Particles() = default;
    [[nodiscard]] bool gas(std::uint32_t material) const noexcept;
    [[nodiscard]] bool solid(std::uint32_t material) const noexcept;
    void land(std::span<std::uint32_t> field, std::size_t particle, std::size_t cell);

    const xengine::matter::Program* program_ = nullptr;
    Shape field_;
    Options options_;
    std::uint32_t channels_ = 0;
    std::optional<std::uint32_t> pressure_;
    std::vector<Vec> position_, velocity_;  // in cells and cells a second
    std::vector<std::uint32_t> cells_;      // [particle][channel]
    Statistics last_, pending_;  // pending: thrown and refused since the last step
};

}  // namespace xengine::cellular::particles
