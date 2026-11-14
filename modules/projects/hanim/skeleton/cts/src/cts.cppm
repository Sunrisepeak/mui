// hanim.skeleton.cts — a crowd of characters held to the world contract (mlibs.cts:world, `hanim.skeleton@1`), and
// the procedural biped the tests and the benchmark play: 23 joints (hips, a spine of three, neck and head, two
// arms of four, two legs of four, and a finger), five clips sampled from formulas at 30 keys a second — idle (breathing), walk
// and run (their hips carrying root motion forward), wave (the right arm, for a layer by mask) and lean (the
// spine forward, for an added layer) — and a machine: idle, walk and run by `speed`, a `halt` trigger back to idle.
//
//     mlibs::cts::against({.subject = "hanim.skeleton"}, [] { hanim::skeleton::cts::declare(hanim::skeleton::cts::Family{}); });
export module hanim.skeleton.cts;

import std;
import mlibs.cts;
export import hanim.skeleton;

export namespace hanim::skeleton::cts {

inline constexpr std::string_view contract = "hanim.skeleton@1";

// The biped's skeleton: 22 joints of the body, then chains of five under the hands up to `joints` (a finger by
// default; a benchmark's larger skeletons).
[[nodiscard]] Skeleton biped(std::size_t joints = 23);
// The biped with its five clips (idle, walk, run, wave, lean), compressed by ACL when `compressed`.
[[nodiscard]] std::shared_ptr<Rig> rig(std::size_t joints = 23, bool compressed = true);
// idle, walk, run; parameters speed (m/s) and halt (a trigger).
[[nodiscard]] std::shared_ptr<Machine> machine(const Rig& rig);
// `characters` characters, in rows, each given a speed of its own (some idle, some walking, some running).
[[nodiscard]] Crowd crowd(std::size_t characters = 8, unsigned workers = 0);

// The world contract's adapter: the crowd, stepped; a disturbance is every character sent to run, a wave layered on
// the even ones and a halt on every third.
struct Family {
    unsigned workers = 0;
    using State = Crowd;
    [[nodiscard]] std::unique_ptr<State> make() const { return std::make_unique<State>(crowd(8, workers)); }
    void step(State& state) const { state.step(); }
    void disturb(State& state) const;
    [[nodiscard]] std::uint64_t hash(const State& state) const { return state.state_hash(); }
    [[nodiscard]] std::vector<std::uint8_t> save(const State& state) const { return state.save(); }
    [[nodiscard]] bool restore(State& state, std::span<const std::uint8_t> image) const { return state.restore(image).has_value(); }
    [[nodiscard]] std::unique_ptr<State> branch(const State& state) const { return std::make_unique<State>(state); }
    void read(const State& state) const;
};

void declare(Family family, mlibs::cts::world::Run run = {.frames = 240, .at = 150});

}  // namespace hanim::skeleton::cts
