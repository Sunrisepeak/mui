// Kiln as a game the runtime plays (xengine.runtime's Playable): the simulation's own state, input and step;
// what a player is shown; an agent player's actions as Mui's.
//
//   kiln::Standalone game(*kiln::start(kiln::level::first()), {.keep = 600});
export module omui.showcase.kiln.headless;

import std;
import xengine.input;
export import xengine.runtime;
export import omui.showcase.kiln.sim;

export namespace kiln {

struct Play {
    using State = kiln::State;
    using Input = kiln::Input;
    static constexpr std::int32_t steps_per_second = kiln::steps_per_second;
    static void step(State& state, const Input& input) { kiln::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return kiln::checksum(state); }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return state.outcome != Outcome::playing; }
    // What a player is shown: Mui, the exit, the cells around her by what they are; the full view adds the hash.
    [[nodiscard]] static std::string observe(const State& state, bool full) { return kiln::observe(state, full); }
};

using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace kiln
