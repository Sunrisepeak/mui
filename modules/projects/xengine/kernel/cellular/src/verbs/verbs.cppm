// xengine.cellular.verbs — what is done to a world of matter, as parts any work uses (the package's feature
// `verbs`; the foundation plan's §6.15, D-183: an engine's standard library, the second of the four levels). An
// explosion, a heat, a pour, a vent: each only places matter where there is none and applies quantities — a
// pressure, a temperature. What follows is the laws': what is brittle shatters, what is loose is thrown, what
// burns is lit. None of them turns one material into another (D-182), and a world counts the cells a work
// overwrote so (rigid::MatterWorld::converted) for whoever wants to hold a work to that. They are done to a
// `Surface`: a world of matter is one, and so is the field an agent's subject stands on — the same verbs are
// the agent's operations (matter.explode, matter.heat, matter.pour).
//
//     verbs::explode(world, {x, y}, {.radius = 6, .pressure = 600000, .heat_radius = 3, .kelvin = 1100});
//     verbs::heat(world, {x, y}, 2, 1000);
//     verbs::pour(world, {x, y}, water, {.radius = 2});
//     verbs::vent(world, spring);                     // each frame: a spring, a chimney, a burner
export module xengine.cellular.verbs;

import std;
export import xengine.cellular;
export import xengine.matter;

export namespace xengine::cellular::verbs {

// What a verb is done to: matter in cells that can be read, written where there is none, and given quantities.
// A world of matter is one (rigid::MatterWorld); so is a field an agent's subject stands on.
class Surface {
public:
    virtual ~Surface() = default;
    [[nodiscard]] virtual const xengine::matter::Program& program() const noexcept = 0;
    [[nodiscard]] virtual Shape shape() const noexcept = 0;
    [[nodiscard]] virtual std::span<const std::uint32_t> cells() const noexcept = 0;  // channel by channel, the material first
    [[nodiscard]] virtual std::uint64_t frames() const noexcept = 0;
    virtual void place(Tile region, std::uint32_t material) = 0;
    virtual void set(std::uint32_t channel, Tile region, std::uint32_t value) = 0;
};

struct Cell {
    std::int64_t x = 0, y = 0;
};

// A pressure over a disc and a heat over the square at its middle. `pressure` is the channel's raw value; the
// heat is in the channel's unit (kelvin). Either may be left out (0).
struct Explosion {
    std::int64_t  radius = 6;
    std::uint32_t pressure = 0;
    std::int64_t  heat_radius = 3;
    double        kelvin = 0;
};
void explode(Surface& world, Cell centre, const Explosion& explosion);

// A temperature set over the square of `radius` about a cell.
void heat(Surface& world, Cell centre, std::int64_t radius, double kelvin);

// A material placed about a cell wherever there is only gas: nothing that is there is replaced. Within the
// square of `radius`, cut to the cells no further than `reach` steps (across plus down) from the middle;
// `reach` 0: the whole square. How many cells it filled.
struct Pour {
    std::int64_t radius = 2;
    std::int64_t reach = 3;
};
std::uint32_t pour(Surface& world, Cell centre, std::uint32_t material, const Pour& pour = {});

// What gives without end: every `every` frames a pour of `material` (none: no pour) and a heat (0: none) at a
// place. Called once a frame, before the world's step.
struct Vent {
    Cell          at;
    std::optional<std::uint32_t> material;
    Pour          pour{.radius = 0, .reach = 0};
    std::int64_t  heat_radius = 0;
    double        kelvin = 0;
    std::uint32_t every = 1;
};
std::uint32_t vent(Surface& world, const Vent& vent);

}  // namespace xengine::cellular::verbs
