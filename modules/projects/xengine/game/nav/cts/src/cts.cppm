// xengine.nav.cts — a navigation world held to the world contract (mlibs.cts:world, `xengine.nav@1`), and the
// scenes the tests and benchmarks walk: a room with a wall across it and a doorway, a platform too high to climb
// and a link up onto it, a crowd sent across.
//
//     mlibs::cts::against({.subject = "recast-detour-1.6"}, [] { xengine::nav::cts::declare(xengine::nav::cts::Family{}); });
export module xengine.nav.cts;

import std;
import mlibs.cts;
export import xengine.nav;

export namespace xengine::nav::cts {

inline constexpr std::string_view contract = "xengine.nav@1";

// Triangles, a box at a time (its faces wound counter-clockwise seen from outside).
struct Builder {
    Ground ground;
    void quad(Vec a, Vec b, Vec c, Vec d);
    void box(Vec low, Vec high);
    void floor(Vec low, Vec high);  // a flat rectangle at low.y
};

// A room 30 m square: a wall 0.4 m thick and 2 m high across x = 0 from z = -15 to 8 (the doorway from 8 to 15);
// a platform 1.5 m high at x 6 … 12, z -12 … -6 (too high to climb).
[[nodiscard]] Ground room();
// The link up onto the platform: from the floor beside it to its top, both ways.
[[nodiscard]] Link ramp_link();
// The room built (with the link), `walkers` walkers in rows of eight on the wall's west side, every other one sent
// through the doorway to the east, the rest up the link onto the platform.
[[nodiscard]] World crowd(int walkers = 8);

// The world contract's adapter: the crowd scene, stepped; a disturbance is an obstacle placed in the doorway and
// every walker sent somewhere else.
struct Family {
    using State = World;
    [[nodiscard]] std::unique_ptr<State> make() const { return std::make_unique<State>(crowd()); }
    void step(State& world) const { world.step(); }
    void disturb(State& world) const;
    [[nodiscard]] std::uint64_t hash(const State& world) const { return world.state_hash(); }
    [[nodiscard]] std::vector<std::uint8_t> save(const State& world) const { return world.save(); }
    [[nodiscard]] bool restore(State& world, std::span<const std::uint8_t> image) const { return world.restore(image).has_value(); }
    [[nodiscard]] std::unique_ptr<State> branch(const State& world) const;
    void read(const State& world) const;
};

// The world contract's cases on `family`, run long enough to see walkers through the doorway and onto the link.
void declare(Family family, mlibs::cts::world::Run run = {.frames = 240, .at = 150});

}  // namespace xengine::nav::cts
