// xengine.matter.cts — a carrier of matter against the reference step (contract id xengine.matter.carrier@1;
// what the step computes is ../CONTRACT.md). A carrier is whoever steps instances and their neighbours its own
// way: the lattice's program, a device's kernel. Each case is a small world of the standard definition — or of
// one written for the case — on a grid; the reference and the subject step it from the same state, and after
// every step every value of every channel is compared. One bit apart fails the case, at the step, the instance
// and the channel it is in.
//
//     suite::declare({.name = "lattice", .carry = [](const matter::Program& program, suite::Grid grid) -> suite::Step {
//         …ready the carrier for the program…
//         return [=](std::span<const std::uint32_t> state, std::span<std::uint32_t> next) { …one step…; return true; };
//     }});
export module xengine.matter.cts;

import std;
export import mlibs.cts;
export import xengine.matter;

export namespace xengine::matter::cts {

// Instances on a grid, instance y × width + x, each beside the four next to it with weight 1. `wrapped`: the
// far edge is beside the near one (both sizes at least 2); else nothing is beyond an edge.
struct Grid {
    std::uint32_t width = 0, height = 0;
    bool wrapped = false;
};
// The grid's pairs, as the reference takes them.
[[nodiscard]] std::vector<Pair> pairs_of(Grid grid);

// One step of a carrier: the state (channel-major) to the next; false: it could not.
using Step = std::function<bool(std::span<const std::uint32_t> state, std::span<std::uint32_t> next)>;

// The carrier under test and what it has ("grid": a grid with nothing beyond its edges; "wrapped": a wrapped one).
struct Subject {
    std::string name = "reference";
    // The carrier readied for a program on a grid — none returned: it could not be. Instances stay where they
    // are: a carrier that also moves them has that off here (movement is the carrier's own, not the step's).
    // No function at all: the reference step itself, on the grid's pairs.
    std::function<Step(const Program&, Grid)> carry;
    std::vector<std::string> has{"grid", "wrapped"};
};

// Every case against `subject`: mtest's conformance cases `<subject>/carrier.<case>` (in a tally, counted).
void declare(Subject subject = {}, mlibs::cts::Against who = {}, std::source_location at = std::source_location::current());
// How many cases the suite has.
[[nodiscard]] std::size_t case_count() noexcept;

}  // namespace xengine::matter::cts
