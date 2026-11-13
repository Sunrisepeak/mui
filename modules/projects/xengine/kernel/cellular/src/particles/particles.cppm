// xengine.cellular.particles — cells of the lattice in flight (the plan's §6.7, lattice ↔ particles): the
// package's feature `particles`. A cell thrown out of the field — blown by a pressure, knocked by a blast —
// flies as a particle carrying its whole state (every channel), falls under gravity, and is a cell again in
// the last gas it passed before it met anything that is not gas. Nothing is made or lost: a cell thrown leaves
// a cell of the fill (a gas) where it was, and one that lands takes the place of a gas.
//
//     auto flying = ca::particles::Particles::make(program, shape, {.cell = 0.1f});
//     flying->scatter(cells, fill);          // liquids and powders where a law of motion moves them, thrown down its channel's slope
//     flying->blast(cells, fill, {x, y}, 6, 40);   // or a blast: every cell that is not solid within 6 cells, outward
//     flying->step(cells);                   // each frame: fly, and land
//
// A material is what the table says: a gas never flies (it is what they fly through), a solid stays (a body's,
// or the terrain's — unless a blast is told to break solids). Determinism: + − × and floor, no contraction, a
// particle's path walked cell by cell in a fixed order; two runs are the same bits.
export module xengine.cellular.particles;
import std;
export import mlibs.math;
export import xengine.cellular.matter;

export namespace xengine::cellular::particles {

using Vec = mlibs::math::Vec2;  // in cells; the rigid family's vector is the same type
struct Options {
    float cell = 0.1f;           // metres a cell
    float gravity = 9.8f;        // m/s², toward +y (the field's rows go down)
    float step = 1.0f / 60.0f;   // seconds a frame
    float drag = 0.02f;          // of its speed a particle loses a frame to the air
    std::uint32_t most = 65536;  // particles in flight at once; past it, what would be thrown stays
    // scatter: whether the definition's laws of motion down a channel's slope throw cells here (a world may
    // keep its cells in the lattice). From what value and how fast is the law's (`from`, `speed`).
    bool thrown = false;
    float fastest = 15.0f;       // m/s: no cell is thrown faster
};
// Why a cell flew: thrown by code (fling), by the field's pressure (scatter), by a blast.
enum class Cause : std::uint8_t { flung, pressure, blast };
// Where a particle came from: the cell it was thrown out of, and the frame its owner said it was then. Carried
// with it (saved, branched) and read by no step: what flies is the same bits with or without it.
struct Origin {
    std::uint32_t cell = 0;
    std::uint64_t frame = 0;
};
// What was thrown and what landed, said to whoever asks (Particles::log): a world's journal (D-169). Nothing is
// said, nor kept, where no one asks.
struct Log {
    struct Thrown {
        std::uint32_t cell = 0, material = 0;
        Cause cause = Cause::flung;
        std::uint32_t law = 0xffffffffu;  // by the field (Cause::pressure): which law of motion
    };
    struct Landed {
        std::uint32_t cell = 0, material = 0;  // where it is a cell again, and of what
        std::uint32_t from = 0;                // the cell it was thrown out of
        std::uint64_t thrown_at = 0;           // and the frame
    };
    std::vector<Thrown> thrown;
    std::vector<Landed> landed;
};
// The last step's: what was thrown since the step before (and refused, no room), what landed, what flies on.
struct Statistics { std::uint64_t thrown = 0, landed = 0, flying = 0, refused = 0; };
inline constexpr std::size_t impacts_a_step = 64;

class Particles {
public:
    [[nodiscard]] static std::expected<Particles, std::string> make(const xengine::matter::Program& program, Shape field, const Options& options = {});

    // A cell thrown with a velocity (m/s): out of the field into flight; `fill` (one instance: a gas) left where
    // it was. False for a gas, a cell out of the field, or no room.
    bool fling(std::span<std::uint32_t> field, std::uint32_t x, std::uint32_t y, Vec velocity, std::span<const std::uint32_t> fill);
    // Every liquid or powder where a law of motion's channel is at least its `from`, thrown down that slope.
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
    [[nodiscard]] std::span<const Origin> origins() const noexcept { return origin_; }
    // Who is told what is thrown and what lands from now on (none: no one), and the frame a throw is said to be
    // of — its owner's count, which the particles do not keep.
    void log(Log* log) noexcept { log_ = log; }
    void now(std::uint64_t frame) noexcept { frame_ = frame; }
    void centres(std::vector<float>& out) const;
    // A particle's channel (its material is channel 0).
    [[nodiscard]] std::uint32_t value(std::size_t particle, std::uint32_t channel) const noexcept { return cells_[particle * channels_ + channel]; }
    [[nodiscard]] const Statistics& last() const noexcept { return last_; }
    // The last step's landings with how fast each came down (cells a second): the first `impacts_a_step` of
    // them, with no one asked to log — what a sound listens to. Not of the state.
    [[nodiscard]] std::span<const xengine::matter::Impact> impacts() const noexcept { return std::span(impacts_).first(impact_count_); }
    // What is in flight, as bytes (a world's save), and back.
    // Without `origins`: what a step reads and nothing else — what a state's hash is of.
    void save(std::vector<std::uint8_t>& out, bool origins = true) const;
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
    struct Push { std::uint32_t law = 0, channel = 0; float from = 0, speed = 0; std::optional<std::uint32_t> scale; };
    std::vector<Push> pushes_;  // the definition's laws of motion down a slope that give a speed
    std::vector<Vec> position_, velocity_;  // in cells and cells a second
    std::vector<std::uint32_t> cells_;      // [particle][channel]
    std::array<xengine::matter::Impact, impacts_a_step> impacts_{};  // the last step's
    std::size_t impact_count_ = 0;
    std::vector<Origin> origin_;            // a particle each
    Log* log_ = nullptr;
    std::uint64_t frame_ = 0;
    std::uint32_t law_ = 0xffffffffu;       // and by which law, where the field made them
    Cause cause_ = Cause::flung;            // of the throws being made
    Statistics last_, pending_;  // pending: thrown and refused since the last step
};

}  // namespace xengine::cellular::particles
