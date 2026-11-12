// The breakout as a game the runtime plays (xengine.runtime's Playable): the
// simulation's own state, input and step; what a player is shown; an agent
// player's actions as the first paddle's.
//
//   xengine::runtime::Runtime<omui::breakout::Play> game(omui::breakout::start(level));
export module omui.showcase.widget_breakout.headless;

import std;
import xengine.input;
import xengine.world;
export import xengine.runtime;
export import omui.showcase.widget_breakout.sim;

export namespace omui::breakout {

struct Play {
    using State = breakout::State;
    using Input = breakout::Input;
    static constexpr std::int32_t steps_per_second = breakout::steps_per_second;
    static void step(State& state, const Input& input) { breakout::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return breakout::checksum(state); }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return state.over != 0; }
    // What a player is shown: the paddle, the balls, the bricks standing, the score. A player as a
    // person is not shown the hidden caches; the full view shows them and the state's generator.
    [[nodiscard]] static std::string observe(const State& state, bool full);
};

// Not `Headless`: in an application that is omui's host without a window (omui::Headless), which the showcase's own tests use.
using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace omui::breakout
