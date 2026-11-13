// xengine.cellular.matter — a matter program lowered to the lattice's (D-165, D-167; the package's feature
// `matter`): the lattice as a carrier of matter. Each cell is an instance, its neighbourhood who it is beside;
// the materials' table is the lattice's table, the laws its transfers and reactions, and — what the reference
// has no say in — cells change places by their density and their phase.
//
//     auto lattice = cellular::lower(*matter::standard());          // channel c of a cell is matter's channel c
//     std::vector<std::uint32_t> cells = …, next(cells.size());     // channel-major, as both kernels hold state
//     lattice->step(shape, cells, next, {.boundary = cellular::nothing_beyond, .tick = tick});
//
// Without movement a step is the reference step's bits (xengine.matter's CONTRACT.md; xengine.matter.cts holds
// a carrier to it), where nothing is beyond the edge (`nothing_beyond`) or the edge is wrapped.
export module xengine.cellular.matter;

import std;
export import xengine.cellular.api;
export import xengine.matter;

export namespace xengine::cellular {

// The edge of a world of matter: beyond it is no material — nothing burns there, conducts or gives oxygen.
inline constexpr Boundary nothing_beyond{Edge::constant, xengine::matter::none};

// The edge of a world open to its surroundings: beyond it is `outside` (one instance's channels, as
// `Program::fresh(1, material)` gives them — air at its temperature): heat, wetness, charge and pressure flow out
// to it and in from it by how well it conducts, and what is beside the edge has it for a neighbour (its oxygen).
// It is a bath: it does not change. `outside` is the caller's, alive for the step.
[[nodiscard]] constexpr Boundary open_to(std::span<const std::uint32_t> outside) noexcept { return {Edge::constant, 0, outside}; }

struct Lowering {
    std::vector<Offset> neighbourhood{cardinal.begin(), cardinal.end()};  // who is beside whom, each with weight 1
    bool moves = true;       // cells change places by density and phase (the exchange stage)
    std::uint32_t seed = 0;  // of a liquid's and a gas's sideways choices
    Limits limits;
};

// How a phase moves on the lattice: a solid and a soft body stay (they are dynamics' to move); a powder falls and
// slides; a liquid also levels; a gas changes places up and sideways.
[[nodiscard]] constexpr std::uint32_t mobility_of(xengine::matter::Phase phase) noexcept {
    switch (phase) {
        case xengine::matter::Phase::solid:
        case xengine::matter::Phase::soft: return 0;
        case xengine::matter::Phase::granular: return falls | slides;
        case xengine::matter::Phase::liquid: return falls | slides | spreads;
        case xengine::matter::Phase::gas: return falls | spreads;
    }
    return 0;
}

// The lattice program of a matter program: its channels in the same order (0 the material), a table of the
// materials' properties as the laws read them and one more column, how each may move.
[[nodiscard]] std::expected<Program, Failure> lower(const xengine::matter::Program& matter, const Lowering& options = {});

// What happened between a step's input and its output, as the reference says it (appended): an instance became
// another material (its law is not known here: the largest number), a channel that names its events left 0 or
// returned to it. Cells that changed places are seen as both.
void events_between(const xengine::matter::Program& matter, std::size_t cells, std::span<const std::uint32_t> input, std::span<const std::uint32_t> output,
                    std::vector<xengine::matter::Event>& events);

// The lattice's happenings (Execution::happened) as the matter program names them. A reaction of the lattice is a
// law of kind reaction or conversion, in the laws' order: `laws[reaction]` is that law.
[[nodiscard]] std::vector<std::uint32_t> laws_of_reactions(const xengine::matter::Program& matter);
// A happening as an event: a law that made the cell another thing, by its own event; a flag that rose, by the
// channel's. Unnamed: the largest number (Event::name) — the law and the cell are still said.
[[nodiscard]] xengine::matter::Event event_of(const xengine::matter::Program& matter, std::span<const std::uint32_t> laws, const Happening& happening) noexcept;

}  // namespace xengine::cellular
