// gongfu:input — a fighting game's reading of a controller (D-85).
//
// A Pad is what is held on one frame: four directions (screen-absolute) and
// six buttons (light, medium, heavy punch and kick). The log keeps the last
// 64 frames as a fighter saw them — directions in numpad notation relative
// to the way it faced (6 forward, 4 back, 2 down, 5 neutral), buttons held
// and pressed — and lives in the simulation's state, so a rollback restores
// what the inputs were. Opposite directions held together are resolved by
// the SOCD policy before anything reads them.
//
// A Command is how a move is asked for: a motion (236 within 12 frames), a
// charge (4 held 40 frames, then 6), a direction held at the press (2 for a
// crouching normal, 6 for a command normal), and the buttons, any of them or
// all of them together, pressed within a buffer of a few frames.
export module gongfu:input;

import std;

export namespace gongfu {

enum class Button : std::uint8_t { lp, mp, hp, lk, mk, hk };
inline constexpr std::size_t button_count = 6;

[[nodiscard]] constexpr std::uint16_t bit(Button b) noexcept { return static_cast<std::uint16_t>(1u << static_cast<unsigned>(b)); }
inline constexpr std::uint16_t punches = 0b000111, kicks = 0b111000, any_button = 0b111111;

// What is held on one frame.
struct Pad {
    static constexpr std::uint16_t up = 1 << 0, down = 1 << 1, left = 1 << 2, right = 1 << 3;
    std::uint16_t directions = 0;  // up, down, left, right
    std::uint16_t buttons = 0;     // bit(Button)
    friend constexpr bool operator==(Pad a, Pad b) noexcept { return a.directions == b.directions && a.buttons == b.buttons; }
};

// A pad from a screen-absolute numpad direction (6 right, 4 left, 8 up,
// 2 down, 5 none) and buttons.
[[nodiscard]] constexpr Pad pad(int numpad, std::uint16_t buttons = 0) noexcept {
    std::uint16_t d = 0;
    if (numpad == 7 || numpad == 8 || numpad == 9) d |= Pad::up;
    if (numpad == 1 || numpad == 2 || numpad == 3) d |= Pad::down;
    if (numpad == 1 || numpad == 4 || numpad == 7) d |= Pad::left;
    if (numpad == 3 || numpad == 6 || numpad == 9) d |= Pad::right;
    return {d, buttons};
}

// Opposite directions held at once.
enum class Socd : std::uint8_t {
    neutral,    // left + right is neither, up + down is neither
    last_wins,  // the one pressed last wins (left + right); up + down is neither
    up_wins,    // left + right is neither; up + down is up (the common tournament rule)
};

// The last 64 frames of a fighter's inputs. Plain data: it is part of the
// state a rollback copies.
struct InputLog {
    static constexpr std::int32_t frames = 64;
    // Per frame: the relative numpad direction (bits 0–3), buttons held
    // (bits 4–9), pressed this frame (bits 10–15) and the screen-absolute
    // direction as given (bits 16–19: what the player did, which a fighter
    // turning around does not change).
    std::array<std::uint32_t, frames> entries{};
    std::int32_t head = 0;             // the newest frame's index
    std::int32_t count = 0;            // frames recorded, up to `frames`
    std::int32_t last_horizontal = 0;  // +1 right, −1 left: pressed last (Socd::last_wins)
    std::int32_t previous = 0;         // the last frame's raw directions and buttons

    // Records one frame, the fighter facing `facing` (+1 right, −1 left).
    void record(Pad p, std::int32_t facing, Socd socd) noexcept;

    // `age` frames ago (0: this frame); a frame never recorded is neutral.
    [[nodiscard]] int direction(std::int32_t age) const noexcept;
    [[nodiscard]] std::uint16_t held(std::int32_t age) const noexcept;
    [[nodiscard]] std::uint16_t pressed(std::int32_t age) const noexcept;
    // The screen-absolute numpad direction given `age` frames ago (5: none).
    [[nodiscard]] int raw(std::int32_t age) const noexcept;
};

// The relative numpad direction of directions held, the fighter facing
// `facing`: right is 6 when it faces right, 4 when it faces left.
[[nodiscard]] int relative(std::uint16_t directions, std::int32_t facing) noexcept;

struct Motion {
    std::array<std::uint8_t, 6> steps{};  // relative numpad directions, in order
    std::uint8_t length = 0;
    std::uint8_t window = 12;  // frames from the first step to the button
};
[[nodiscard]] constexpr Motion motion(std::initializer_list<int> steps, int window = 12) noexcept {
    Motion m;
    for (int s : steps) m.steps[m.length++] = static_cast<std::uint8_t>(s);
    m.window = static_cast<std::uint8_t>(window);
    return m;
}
inline constexpr Motion quarter_forward = motion({2, 3, 6});   // 236
inline constexpr Motion quarter_back = motion({2, 1, 4});      // 214
inline constexpr Motion dragon = motion({6, 2, 3});            // 623
inline constexpr Motion half_back = motion({6, 3, 2, 1, 4}, 20);  // 63214
inline constexpr Motion dash_forward = motion({6, 5, 6}, 10);   // 66: tapped twice
inline constexpr Motion dash_back = motion({4, 5, 4}, 10);      // 44

// Back (1, 4 or 7) or down (1, 2, 3) held, then the other way.
struct Charge {
    std::uint8_t from = 0;     // 4: back, then forward; 2: down, then up
    std::uint8_t frames = 40;  // held this long
    std::uint8_t window = 10;  // the release direction within this many frames of letting go
};

struct Command {
    Motion        motion;          // none when its length is 0
    Charge        charge;          // none when `from` is 0
    std::uint8_t  direction = 0;   // held at the press: 0 any, 2 down (1, 2, 3), 6 forward, 4 back, 8 up
    std::uint16_t buttons = 0;     // bit(Button): any of them, or all (`together`); none: the motion alone (a dash, 66)
    bool          together = false;
    std::uint8_t  buffer = 3;      // pressed within this many frames before now
};

// Whether `log` holds `command` now; the press's age when it does.
[[nodiscard]] std::optional<std::int32_t> recognise(const InputLog& log, const Command& command) noexcept;

}  // namespace gongfu
