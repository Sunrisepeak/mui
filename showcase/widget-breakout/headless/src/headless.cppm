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
    // The rules a player is told before playing.
    static constexpr std::string_view about =
        "Breakout: keep the ball in play with the paddle and break every brick; a ball that falls past the paddle costs a life; some bricks are "
        "hidden until hit, and a broken brick may give a gift (a wider paddle, more balls, slow motion, a ball that goes through). Three lives. The game is "
        "over when the bricks are cleared or the lives are gone";
    // What the controls mean, and what to begin with (D-186): `world.state` lists them, `find` answers "launch".
    static constexpr std::array<xengine::runtime::Control, 5> controls{{
        {.name = "launch", .summary = "Launch the ball waiting on the paddle", .button = launch_button},
        {.name = "look", .summary = "Held: look at the whole field, a taller one", .button = look_button},
        {.name = "left", .summary = "The paddle moves left", .axis = 0, .toward = -1},
        {.name = "right", .summary = "The paddle moves right", .axis = 0, .toward = 1},
        {.name = "paddle", .summary = "The paddle goes to where the pointer is across the field: [x, y], x 0 (left) to 1 (right)", .pointer = true},
    }};
    static constexpr std::array<xengine::runtime::Move, 2> launch_it{{{.hold = {"launch"}, .steps = 20}, {.steps = 60}}};
    static constexpr std::array<xengine::runtime::Move, 1> to_the_left{{{.hold = {"paddle"}, .steps = 90, .pointer = {0.2, 0.5}}}};
    static constexpr std::array<xengine::runtime::Task, 2> tasks{{
        {"launch-the-ball", "Launch the ball waiting on the paddle and see where it went", launch_it},
        {"move-the-paddle", "Send the paddle to the left of the field and see where it stands", to_the_left},
    }};
};

// Not `Headless`: in an application that is omui's host without a window (omui::Headless), which the showcase's own tests use.
using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace omui::breakout
