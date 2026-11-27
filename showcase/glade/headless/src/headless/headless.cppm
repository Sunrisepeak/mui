// Glade as a game the runtime plays (meng.runtime's Playable; M6.5 T5.8b): the valley poked by a hand a
// person or an agent holds over it — aimed with the pointer, a verb a button — and stepped, sought back and
// branched ahead with no interface. What a player is shown is the semantic projection (glade::observe) with the
// goals; shown everything, it adds why each changed thing is what it became (glade::explain).
//
//   glade::Standalone game(glade::begin(), {.tier = …});
//   mlibs::agent::SubjectSession session({&game});
//   → {"op":"world.branch","steps":600,"buttons":[0],"pointer":[0.32,0.13]}   a torch held at the campfire, what would be
export module omui.showcase.glade.headless;

import std;
import meng.input;
export import meng.runtime;
export import omui.showcase.glade.sim;

export namespace glade {

// The hand's buttons and axes.
inline constexpr int torch_button = 0, pour_button = 1, push_button = 2, rain_button = 3, strike_button = 4, rod_button = 5;
inline constexpr int push_x_axis = 0, push_z_axis = 1, wind_x_axis = 2, wind_z_axis = 3;
// The pointer's square over the valley: x from −10 to 12 m, z from −4 to 4 m (the level's ground).
inline constexpr float aim_x0 = -10, aim_width = 22, aim_z0 = -4, aim_depth = 8;
inline constexpr float push_impulse = 200;  // N·s, a shove of the hand
inline constexpr float wind_speed = 8;      // m/s at a full axis
inline constexpr int bucket = 40;           // drops a pour lets fall

// The place on the ground under a pointer ([0, 1] each way), and the pointer over a place.
[[nodiscard]] Vec3 aimed(std::int16_t pointer_x, std::int16_t pointer_y);
[[nodiscard]] std::array<double, 2> pointer_at(Vec3 place) noexcept;

struct Play {
    struct State {
        glade::State  valley;
        std::uint32_t held = 0;  // the buttons held the step before: a press is one held now and not then
    };
    struct Input {
        meng::Actions hand;
    };
    static constexpr std::int32_t steps_per_second = glade::steps_per_second;
    static void step(State& state, const Input& input);
    [[nodiscard]] static std::uint64_t checksum(const State& state);
    static void play(Input& input, const meng::Actions& actions) { input.hand = actions; }
    // Every goal met.
    [[nodiscard]] static bool over(const State& state);
    // What a player is shown, as JSON: the step, the weather, each thing as seen, the goals and whether each is
    // met; the full view adds the hash and, for each thing no longer as it began, why it is what it is.
    [[nodiscard]] static std::string observe(const State& state, bool full);
    static constexpr std::string_view about =
        "Glade: a pocket valley — a stream with a footbridge, grass on both banks, a campfire, a wooden crate across the stream, a block of ice — made "
        "of matter. Nothing is scripted: wood and grass burn, water wets and puts fire out, fire melts ice, wind carries heat, lightning strikes what "
        "conducts and stands open. A hand over the valley pokes it: aim with the pointer, hold a torch, pour a bucket, push a thing, call rain, call "
        "lightning, stand an iron rod. The goals are the view's `goals`, each with the verbs it may be met with and where the hand may act; every "
        "answer can be asked why (shown everything, each changed thing says it)";
    static constexpr std::array<meng::runtime::Control, 15> controls{{
        {.name = "aim", .summary = "Where the hand is: a place over the valley, [x, y], x from −10 to 12 m along it, y from −4 to 4 m across", .pointer = true},
        {.name = "torch", .summary = "Hold a torch to the thing nearest the hand (within 0.6 m) while held; let go, it is taken away", .button = torch_button},
        {.name = "pour", .summary = "Pour a bucket of water (40 drops) over the place, once a press", .button = pour_button},
        {.name = "push", .summary = "Shove the loose thing nearest the hand (within 1 m) the way the push axes say, once a press", .button = push_button},
        {.name = "rain", .summary = "Call the rain, or stop it, once a press", .button = rain_button},
        {.name = "lightning", .summary = "Call lightning, once a press: it strikes the highest open thing that conducts, if any", .button = strike_button},
        {.name = "stand-rod", .summary = "Stand an iron rod 2 m tall at the place, once a press", .button = rod_button},
        {.name = "push-west", .summary = "Push toward lower x (with push)", .axis = push_x_axis, .toward = -1},
        {.name = "push-east", .summary = "Push toward higher x (with push)", .axis = push_x_axis, .toward = 1},
        {.name = "push-south", .summary = "Push toward lower z (with push)", .axis = push_z_axis, .toward = -1},
        {.name = "push-north", .summary = "Push toward higher z (with push)", .axis = push_z_axis, .toward = 1},
        {.name = "wind-west", .summary = "Wind toward lower x while held", .axis = wind_x_axis, .toward = -1},
        {.name = "wind-east", .summary = "Wind toward higher x while held", .axis = wind_x_axis, .toward = 1},
        {.name = "wind-south", .summary = "Wind toward lower z while held", .axis = wind_z_axis, .toward = -1},
        {.name = "wind-north", .summary = "Wind toward higher z while held", .axis = wind_z_axis, .toward = 1},
    }};
    // The campfire's middle log is at (−2.8, −3): x (−2.8 + 10) / 22, y (−3 + 4) / 8.
    static constexpr std::array<meng::runtime::Move, 2> torch_the_campfire{{{.hold = {"aim", "torch"}, .steps = 180, .pointer = {0.32727, 0.125}}, {.steps = 120}}};
    static constexpr std::array<meng::runtime::Move, 2> rain_a_while{{{.hold = {"rain"}, .steps = 1}, {.steps = 300}}};
    static constexpr std::array<meng::runtime::Task, 2> tasks{{
        {"light-the-campfire", "Hold a torch to the campfire's logs for three seconds and see them burn", torch_the_campfire},
        {"call-the-rain", "Call the rain and see the valley wet after five seconds", rain_a_while},
    }};
};

using Standalone = meng::runtime::Runtime<Play>;

// The valley as it begins.
[[nodiscard]] Play::State begin();

}  // namespace glade
