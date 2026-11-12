// xengine.input — devices to a game's actions, sampled once a frame.
//
//     xengine::Bindings b;
//     b.key(key::left, xengine::Axis{0, -1});      // axis 0 toward −1
//     b.key(key::space, xengine::Button{0});       // button 0: launch
//     b.pointer_x(xengine::Axis{1, 1});            // axis 1: where the pointer is, across
//     xengine::Sampler in(b);
//     in.key(code, true);                        // as the host delivers them
//     const xengine::Actions a = in.sample();      // once a frame: plain data
//
// Held, pressed (down since the last sample) and released bits for up to 32
// buttons; four axes in 16-bit steps (−32767 … 32767); the pointer in
// [0, 1] × [0, 1] of the game's area, in 16-bit steps. A press and a release
// between two samples is still a press (and a release): a tap shorter than a
// frame is not lost — what "input latency ≤ 1 frame" needs.
export module xengine.input;

import std;

export namespace xengine {

struct Actions {
    std::uint32_t                held = 0, pressed = 0, released = 0;
    std::array<std::int16_t, 4> axes{};
    std::int16_t                 pointer_x = -1, pointer_y = -1;  // −1: no pointer
    std::int16_t                 wheel = 0;  // the wheel's notches this frame (+ away from the user)
    std::int16_t                 spare = 0;
    [[nodiscard]] constexpr bool down(int button) const noexcept { return (held >> button) & 1u; }
    [[nodiscard]] constexpr bool went_down(int button) const noexcept { return (pressed >> button) & 1u; }
    [[nodiscard]] constexpr double axis(int i) const noexcept { return axes[static_cast<std::size_t>(i)] / 32767.0; }
    // Written out: GCC 16 fails on a defaulted friend comparison of an imported type.
    friend constexpr bool operator==(const Actions& a, const Actions& b) noexcept {
        return a.held == b.held && a.pressed == b.pressed && a.released == b.released && a.axes == b.axes && a.pointer_x == b.pointer_x &&
               a.pointer_y == b.pointer_y && a.wheel == b.wheel && a.spare == b.spare;
    }
};
static_assert(std::has_unique_object_representations_v<Actions>, "actions are recorded and hashed as bytes");

struct Button {
    std::int32_t index = 0;
};
struct Axis {
    std::int32_t index = 0;
    std::int32_t sign = 1;  // a key pushes the axis toward −1 or +1
};

// What each device input means to the game.
class Bindings {
public:
    void key(std::uint32_t code, Button b) { keys_.push_back({code, b.index, -1, 0}); }
    void key(std::uint32_t code, Axis a) { keys_.push_back({code, -1, a.index, a.sign}); }
    void pad_button(std::int32_t index, Button b) { pads_.push_back({static_cast<std::uint32_t>(index), b.index, -1, 0}); }
    void pad_button(std::int32_t index, Axis a) { pads_.push_back({static_cast<std::uint32_t>(index), -1, a.index, a.sign}); }  // a d-pad
    void pad_axis(std::int32_t index, Axis a) { axes_.push_back({static_cast<std::uint32_t>(index), -1, a.index, a.sign}); }
    void mouse(std::int32_t button, Button b) { mice_.push_back({static_cast<std::uint32_t>(button), b.index, -1, 0}); }  // 1 left, 2 right, 3 middle
    void pointer_x(Axis a) { pointer_x_ = a; }

private:
    friend class Sampler;
    struct Binding {
        std::uint32_t source;
        std::int32_t  button, axis, sign;
    };
    std::vector<Binding> keys_, pads_, axes_, mice_;
    std::optional<Axis>  pointer_x_;
};

class Sampler {
public:
    explicit Sampler(Bindings bindings) : b_(std::move(bindings)) {}
    // As the host delivers them, between samples.
    void key(std::uint32_t code, bool down);
    void pad_button(std::int32_t index, bool down);
    void pad_axis(std::int32_t index, double value);  // −1 … 1
    void pointer(double x, double y);                 // 0 … 1 of the game's area; outside: gone
    void wheel(std::int32_t notches);                 // + away from the user
    void mouse(std::int32_t button, bool down);       // 1 left, 2 right, 3 middle
    void clear();                                     // focus lost: nothing held
    // This frame's actions; pressed and released start again.
    [[nodiscard]] Actions sample();

private:
    void button(std::int32_t index, bool down);
    Bindings                          b_;
    std::uint32_t                     held_ = 0, pressed_ = 0, released_ = 0;
    std::vector<std::uint32_t>        keys_down_;
    std::uint64_t                     pads_down_ = 0;  // pad buttons held (by index < 64)
    std::array<double, 8>             pad_axes_{};
    std::array<std::int32_t, 4>       key_axes_{};
    std::int16_t                      px_ = -1, py_ = -1;
    std::int32_t                      wheel_ = 0;
};

// Actions as text (one frame a line: "held pressed released a0 a1 a2 a3 px py wheel"),
// and back: a recording people and agents can read and write.
[[nodiscard]] std::string to_text(std::span<const Actions> frames);
[[nodiscard]] std::expected<std::vector<Actions>, std::string> from_text(std::string_view text);


// ── Agent players (convergence plan §8.4) ─────────────────────────────────
// An agent playing as a person plays through the same actions (a held axis,
// pressed buttons), held to a tier the game declares: what it is shown, how
// soon its actions can take effect after what it saw, how often it may act.
struct PlayTier {
    std::string_view name;                 // "human": as a person; "assist": privileged
    std::int32_t     reaction_steps = 0;   // an action takes effect no sooner than this many steps after the step it saw
    std::int32_t     actions_per_second = 0;  // at most this many actions a second of steps (0: no limit)
    bool             full_view = false;    // shown everything (hidden state too), not only what is drawn
};

// An agent's lane of actions on its tier. Each action says the step it saw;
// it takes effect at that step plus the tier's reaction, or at the next step
// when that has passed, and holds — like a key held — until the next one.
// Deterministic: the same submissions at the same steps give the same steps'
// actions, so a game records and replays them as a person's.
class AgentLane {
public:
    enum class Refusal : std::uint8_t { unseen, too_often };
    AgentLane(PlayTier tier, std::int32_t steps_per_second);
    // At step `now`, an action decided on what step `seen` showed: the step it
    // takes effect, or why not (it saw a step not yet played; over its rate).
    [[nodiscard]] std::expected<std::int64_t, Refusal> submit(std::int64_t now, std::int64_t seen, Actions actions);
    // The agent's actions for step `step` (called once a step, in order):
    // what it holds, with pressed and released at the step they change.
    [[nodiscard]] Actions take(std::int64_t step);
    [[nodiscard]] const PlayTier& tier() const noexcept { return tier_; }
    [[nodiscard]] std::size_t pending() const noexcept { return queue_.size(); }

private:
    struct Due {
        std::int64_t step;
        Actions      actions;
    };
    PlayTier             tier_;
    std::int32_t         steps_per_second_;
    std::deque<Due>      queue_;
    std::deque<std::int64_t> accepted_;  // the steps of the actions accepted within the last second
    Actions              held_{};
};
}  // namespace xengine
