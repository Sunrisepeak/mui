module gongfu.data;

import std;
import gongfu;

namespace gongfu::data {

namespace {

struct Named {
    std::string_view name;
    std::uint16_t    buttons;
};
constexpr std::array<Named, 8> button_names{{
    {"LP", bit(Button::lp)}, {"MP", bit(Button::mp)}, {"HP", bit(Button::hp)}, {"LK", bit(Button::lk)},
    {"MK", bit(Button::mk)}, {"HK", bit(Button::hk)}, {"P", punches}, {"K", kicks},
}};

// The motions a fighting game names, with their windows.
int default_window(std::size_t steps) { return steps >= 5 ? 20 : steps == 4 ? 16 : 12; }

std::optional<std::uint16_t> button(std::string_view token) {
    for (const Named& n : button_names)
        if (n.name == token) return n.buttons;
    return std::nullopt;
}

}  // namespace

std::string Diagnostic::text() const {
    return std::format("{}:{}:{}: {}{}", file, line, column, message, hint.empty() ? "" : std::format(" ({})", hint));
}

std::expected<Command, std::string> parse_command(std::string_view s) {
    Command c;
    std::size_t i = 0;
    auto fail = [&](std::string why) -> std::expected<Command, std::string> {
        return std::unexpected(std::format("`{}`: {}", s, why));
    };
    // A charge: [4] or [2], or [4:60] with its frames.
    if (i < s.size() && s[i] == '[') {
        const std::size_t close = s.find(']', i);
        if (close == std::string_view::npos) return fail("a charge opened with [ and never closed");
        std::string_view inside = s.substr(i + 1, close - i - 1);
        const std::size_t colon = inside.find(':');
        const std::string_view dir = inside.substr(0, colon);
        if (dir != "4" && dir != "2") return fail("a charge is held back [4] or down [2]");
        c.charge.from = static_cast<std::uint8_t>(dir[0] - '0');
        if (colon != std::string_view::npos) {
            int frames = 0;
            const std::string_view n = inside.substr(colon + 1);
            const auto r = std::from_chars(n.data(), n.data() + n.size(), frames);
            if (r.ec != std::errc{} || r.ptr != n.data() + n.size() || frames < 1 || frames > 60)
                return fail("a charge's frames are 1–60 ([4:45])");
            c.charge.frames = static_cast<std::uint8_t>(frames);
        }
        i = close + 1;
        const char release = c.charge.from == 4 ? '6' : '8';
        if (i >= s.size() || s[i] != release) return fail(std::format("a [{}] charge is released {}", c.charge.from, release));
        ++i;
    }
    // Directions: one held at the press, or several, a motion.
    std::string digits;
    while (i < s.size() && s[i] >= '1' && s[i] <= '9') digits += s[i++];
    if (c.charge.from != 0 && !digits.empty()) return fail("a charge is followed by its release and the button");
    if (digits.size() == 1) {
        const char d = digits[0];
        if (d == '5') return fail("5 is neutral: write the button alone");
        c.direction = static_cast<std::uint8_t>(d - '0');
    } else if (digits.size() > 1) {
        if (digits.size() > c.motion.steps.size()) return fail(std::format("a motion of at most {} directions", c.motion.steps.size()));
        for (char d : digits) c.motion.steps[c.motion.length++] = static_cast<std::uint8_t>(d - '0');
        c.motion.window = static_cast<std::uint8_t>(default_window(digits.size()));
    }
    // Buttons: one, or several together (+).
    const std::string_view rest = s.substr(i);
    if (rest.empty()) return fail("no button");
    std::size_t start = 0;
    int count = 0;
    while (start <= rest.size()) {
        const std::size_t plus = rest.find('+', start);
        const std::string_view token = rest.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start);
        const auto b = button(token);
        if (!b) return fail(std::format("`{}` is not a button (LP MP HP LK MK HK, P any punch, K any kick)", token));
        c.buttons |= *b;
        ++count;
        if (plus == std::string_view::npos) break;
        start = plus + 1;
    }
    c.together = count > 1;
    return c;
}

std::string notation(const Command& c) {
    std::string out;
    if (c.charge.from != 0) {
        out += std::format("[{}", c.charge.from);
        if (c.charge.frames != Charge{}.frames) out += std::format(":{}", c.charge.frames);
        out += std::format("]{}", c.charge.from == 4 ? 6 : 8);
    }
    for (std::size_t k = 0; k < c.motion.length; ++k) out += static_cast<char>('0' + c.motion.steps[k]);
    if (c.direction != 0) out += static_cast<char>('0' + c.direction);
    std::string buttons;
    auto add = [&](std::string_view name) { buttons += std::format("{}{}", buttons.empty() ? "" : "+", name); };
    std::uint16_t left = c.buttons;
    if (!c.together) {
        if (left == punches) return out + "P";
        if (left == kicks) return out + "K";
    }
    for (std::size_t k = 0; k < 6; ++k)
        if (left & button_names[k].buttons) add(button_names[k].name);
    return out + buttons;
}

}  // namespace gongfu::data
