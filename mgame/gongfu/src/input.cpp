module gongfu;

import std;

namespace gongfu {

namespace {

constexpr std::uint16_t direction_mask = 0xF;

bool in(int d, std::initializer_list<int> set) { return std::ranges::find(set, d) != set.end(); }

// Whether `d` is the direction a command or a charge asks for: down and up
// include their diagonals; forward and back are exact for a command normal.
bool held_class(int d, int want) {
    switch (want) {
        case 0: return true;
        case 2: return in(d, {1, 2, 3});
        case 8: return in(d, {7, 8, 9});
        default: return d == want;
    }
}

}  // namespace

int relative(std::uint16_t directions, std::int32_t facing) noexcept {
    const int x = (directions & Pad::right) ? 1 : (directions & Pad::left) ? -1 : 0;
    const int y = (directions & Pad::up) ? 1 : (directions & Pad::down) ? -1 : 0;
    return 5 + x * (facing < 0 ? -1 : 1) + 3 * y;
}

void InputLog::record(Pad p, std::int32_t facing, Socd socd) noexcept {
    const auto before = static_cast<std::uint16_t>(previous & direction_mask);
    std::uint16_t d = p.directions & direction_mask;
    const bool l = d & Pad::left, r = d & Pad::right;
    if (r && !(before & Pad::right)) last_horizontal = 1;
    if (l && !(before & Pad::left)) last_horizontal = (r && !(before & Pad::right)) ? 0 : -1;
    if (l && r) {
        d &= static_cast<std::uint16_t>(~(Pad::left | Pad::right));
        if (socd == Socd::last_wins && last_horizontal != 0) d |= last_horizontal > 0 ? Pad::right : Pad::left;
    }
    if ((d & Pad::up) && (d & Pad::down)) {
        d &= static_cast<std::uint16_t>(~(Pad::up | Pad::down));
        if (socd == Socd::up_wins) d |= Pad::up;
    }
    const std::uint16_t held_now = p.buttons & any_button;
    const auto held_before = static_cast<std::uint16_t>((previous >> 4) & any_button);
    const auto pressed_now = static_cast<std::uint16_t>(held_now & ~held_before);
    head = (head + 1) % frames;
    entries[static_cast<std::size_t>(head)] = static_cast<std::uint32_t>(relative(d, facing)) | (static_cast<std::uint32_t>(held_now) << 4) |
                                              (static_cast<std::uint32_t>(pressed_now) << 10) |
                                              (static_cast<std::uint32_t>(relative(d, 1)) << 16);
    count = std::min(count + 1, frames);
    previous = static_cast<std::int32_t>((p.directions & direction_mask) | (held_now << 4));
}

int InputLog::direction(std::int32_t age) const noexcept {
    if (age < 0 || age >= count) return 5;
    return entries[static_cast<std::size_t>((head - age + frames) % frames)] & 0xF;
}

std::uint16_t InputLog::held(std::int32_t age) const noexcept {
    if (age < 0 || age >= count) return 0;
    return static_cast<std::uint16_t>((entries[static_cast<std::size_t>((head - age + frames) % frames)] >> 4) & any_button);
}

std::uint16_t InputLog::pressed(std::int32_t age) const noexcept {
    if (age < 0 || age >= count) return 0;
    return static_cast<std::uint16_t>((entries[static_cast<std::size_t>((head - age + frames) % frames)] >> 10) & any_button);
}

int InputLog::raw(std::int32_t age) const noexcept {
    if (age < 0 || age >= count) return 5;
    return static_cast<int>((entries[static_cast<std::size_t>((head - age + frames) % frames)] >> 16) & 0xF);
}

std::optional<std::int32_t> recognise(const InputLog& log, const Command& c) noexcept {
    // The press: one of the buttons (or all, pressed within two frames of
    // each other and held together), at most `buffer` frames ago.
    std::optional<std::int32_t> press;
    // No button (a dash, 66): the press is the motion's last direction, newly
    // entered, at most `buffer` frames ago.
    if (c.buttons == 0) {
        if (c.motion.length == 0) return std::nullopt;
        const int last = c.motion.steps[static_cast<std::size_t>(c.motion.length - 1)];
        for (std::int32_t age = 0; age <= c.buffer && !press; ++age)
            if (log.direction(age) == last && log.direction(age + 1) != last) press = age;
    }
    for (std::int32_t age = 0; age <= c.buffer && !press && c.buttons != 0; ++age) {
        if (!(log.pressed(age) & c.buttons)) continue;
        if (c.together) {
            std::uint16_t seen = 0;
            for (std::int32_t a = age; a <= age + 2; ++a) seen |= log.pressed(a);
            if ((seen & c.buttons) != c.buttons || (log.held(age) & c.buttons) != c.buttons) continue;
        }
        press = age;
    }
    if (!press) return std::nullopt;
    if (!held_class(log.direction(*press), c.direction)) return std::nullopt;

    // A rotation (360, 720): the four sides passed, a turn for each, within the window back from the press.
    if (c.motion.rotation > 0) {
        int turns = 0;
        std::uint8_t sides = 0;  // up 1, down 2, back 4, forward 8
        for (std::int32_t age = *press + c.motion.window; age >= *press; --age) {
            const int d = log.direction(age);
            if (d >= 7) sides |= 1;
            if (d <= 3) sides |= 2;
            if (d == 1 || d == 4 || d == 7) sides |= 4;
            if (d == 3 || d == 6 || d == 9) sides |= 8;
            if (sides == 15) {
                ++turns;
                sides = 0;
                // The side it is on starts the next turn.
                if (d >= 7) sides |= 1;
                if (d <= 3) sides |= 2;
                if (d == 1 || d == 4 || d == 7) sides |= 4;
                if (d == 3 || d == 6 || d == 9) sides |= 8;
            }
        }
        if (turns < c.motion.rotation) return std::nullopt;
    }
    // A motion: its steps in order, the last first, back from the press.
    if (c.motion.length > 0) {
        int k = c.motion.length - 1;
        for (std::int32_t age = *press; age <= *press + c.motion.window && k >= 0; ++age)
            if (log.direction(age) == c.motion.steps[static_cast<std::size_t>(k)]) --k;
        if (k >= 0) return std::nullopt;
    }

    // A charge: the release direction, and before it the charge held long enough.
    if (c.charge.from != 0) {
        const auto charged = [&](int d) { return c.charge.from == 4 ? in(d, {1, 4, 7}) : in(d, {1, 2, 3}); };
        const auto released = [&](int d) { return c.charge.from == 4 ? in(d, {3, 6, 9}) : in(d, {7, 8, 9}); };
        std::optional<std::int32_t> release;
        for (std::int32_t age = *press; age <= *press + c.charge.window && !release; ++age)
            if (released(log.direction(age))) release = age;
        if (!release) return std::nullopt;
        std::optional<std::int32_t> end;
        for (std::int32_t age = *release + 1; age <= *release + c.charge.window && !end; ++age)
            if (charged(log.direction(age))) end = age;
        if (!end) return std::nullopt;
        std::int32_t held = 0;
        for (std::int32_t age = *end; age < log.count && charged(log.direction(age)); ++age) ++held;
        if (held < c.charge.frames) return std::nullopt;
    }
    return press;
}

}  // namespace gongfu
