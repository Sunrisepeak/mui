// Quay as a game the runtime plays (xengine.runtime's Playable): the simulation's own state, input and step, and
// what a player is shown — at a person's tier, only what the watchkeeper sees and hears.
//
//   quay::Standalone game(quay::start(quay::level::harbour()), {.keep = 600});
//
// What a person is shown (`observe(state, false)`), all of it from where the watchkeeper stands and looks — angles
// in degrees, a bearing to the right positive, an elevation above where the eyes look positive:
//   keeper   health, rounds, whether a shot just struck, how far along the beacon's lighting is
//   tide     seconds before it covers the breakwater
//   beacon   its lamp: bearing, elevation, distance (it burns at the end of the breakwater, seen from anywhere)
//   wardens  those in view (within the view's angle, nothing between): bearing, elevation, distance — nearest first
//   heard    the last half second's sounds that reach the watchkeeper's ears (xengine.audio.space's `reach`, the rule
//            the speakers play by and the wardens hear by): what, bearing, level at the ears, walls on the way
//   depth    how far the eye meets something, at waist height, across the view: what a person sees of the way ahead
// The full view (`observe(state, true)`, the `assist` tier) adds where everything is, each warden's mind, the hash.
export module omui.showcase.quay.headless;

import std;
export import xengine.runtime;
export import omui.showcase.quay.sim;

export namespace quay {

// What a person sees: the view's half angle across (a 100° view), how far a warden is made out at night in the
// rain, and the depth samples across the view.
inline constexpr float view_half_angle = 50, seen_within = 40;
inline constexpr int depth_from = -60, depth_every = 10, depth_count = 13;
inline constexpr float depth_reach = 30;

[[nodiscard]] std::string observe(const State& state, bool full);

struct Play {
    using State = quay::State;
    using Input = quay::Input;
    static constexpr std::int32_t steps_per_second = quay::steps_per_second;
    static void step(State& state, const Input& input) { quay::step(state, input); }
    [[nodiscard]] static std::uint64_t checksum(const State& state) noexcept { return quay::checksum(state); }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    [[nodiscard]] static bool over(const State& state) noexcept { return state.outcome != Outcome::playing; }
    [[nodiscard]] static std::string observe(const State& state, bool full) { return quay::observe(state, full); }
    static constexpr std::string_view about =
        "Quay: a watchkeeper on a harbour at night has to light the beacon at the end of the breakwater before the tide covers it. Rusted wardens "
        "patrol between the containers; they know only what they hear and see — a shot is heard far, running feet further than walking ones, and "
        "steel between dulls a sound — and once one sees the watchkeeper it gives chase and fires. Three hits stop a warden. Standing within 2.2 m "
        "of the beacon and working it (`use`) for a second lights it and wins; no health, the sea or the tide loses. A person is shown only what "
        "the watchkeeper sees and hears: bearings to the right are positive, in degrees";
    // What the controls mean (D-186): `world.state` lists them, `find` answers "shoot".
    static constexpr std::array<xengine::runtime::Control, 12> controls{{
        {.name = "walk-forward", .summary = "Walk the way the watchkeeper faces", .axis = 1, .toward = 1},
        {.name = "walk-back", .summary = "Walk backward", .axis = 1, .toward = -1},
        {.name = "strafe-right", .summary = "Step sideways to the right", .axis = 0, .toward = 1},
        {.name = "strafe-left", .summary = "Step sideways to the left", .axis = 0, .toward = -1},
        {.name = "turn-right", .summary = "Turn to the right (149° a second at full)", .axis = 2, .toward = 1},
        {.name = "turn-left", .summary = "Turn to the left", .axis = 2, .toward = -1},
        {.name = "look-up", .summary = "Look up (103° a second at full)", .axis = 3, .toward = 1},
        {.name = "look-down", .summary = "Look down", .axis = 3, .toward = -1},
        {.name = "jump", .summary = "Jump from the ground", .button = jump_button},
        {.name = "fire", .summary = "Fire where the watchkeeper looks; held, about seven shots a second while rounds last", .button = fire_button},
        {.name = "use", .summary = "Work the beacon, within 2.2 m of it: a second's work lights it", .button = use_button},
        {.name = "run", .summary = "Run while walking: quicker, harder to hit, and heard further", .button = run_button},
    }};
    static constexpr std::array<xengine::runtime::Move, 2> look_around{{{.hold = {"turn-right"}, .steps = 145}, {.steps = 12}}};
    static constexpr std::array<xengine::runtime::Move, 2> walk_on{{{.hold = {"walk-forward"}, .steps = 90}, {.steps = 12}}};
    static constexpr std::array<xengine::runtime::Task, 2> tasks{{
        {"look-around", "Turn a full circle where the watchkeeper stands and see what is in view on the way back", look_around},
        {"walk-on", "Walk on a second and a half the way the watchkeeper faces, and see what is ahead", walk_on},
    }};
};

using Standalone = xengine::runtime::Runtime<Play>;

}  // namespace quay
