// openmui.core:keys — keys, modifiers, and the chords contracts bind.
//
// Contracts write key bindings as text ("Right|Up", "Alt+Down", "Mod+S");
// `Mod` is the platform's primary modifier (Ctrl, or Cmd on macOS), chosen by
// the host, so a contract never names a platform.
export module openmui.core:keys;

import std;

export namespace omui {

enum class Key : std::uint16_t {
    unknown,
    a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
    digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    enter, escape, backspace, tab, space,
    minus, equal, left_bracket, right_bracket, backslash, semicolon, apostrophe, grave, comma, period, slash,
    insert, delete_, home, end, page_up, page_down,
    left, right, up, down,
    caps_lock, print_screen, scroll_lock, pause, menu,
    shift, control, alt, meta,
    count_,
};

enum class Modifier : std::uint8_t { none = 0, shift = 1, ctrl = 2, alt = 4, meta = 8 };

struct Modifiers {
    std::uint8_t bits = 0;

    constexpr Modifiers() = default;
    constexpr Modifiers(Modifier m) : bits(static_cast<std::uint8_t>(m)) {}
    [[nodiscard]] constexpr bool has(Modifier m) const noexcept { return (bits & static_cast<std::uint8_t>(m)) != 0; }
    friend constexpr Modifiers operator|(Modifiers a, Modifiers b) noexcept {
        Modifiers r;
        r.bits = a.bits | b.bits;
        return r;
    }
    friend constexpr bool operator==(Modifiers a, Modifiers b) noexcept { return a.bits == b.bits; }
};

[[nodiscard]] constexpr Modifiers operator|(Modifier a, Modifier b) noexcept { return Modifiers(a) | Modifiers(b); }

[[nodiscard]] std::string_view name(Key k) noexcept;      // "A", "PageUp", "F5"
[[nodiscard]] std::optional<Key> parse_key(std::string_view text) noexcept;

// What `Mod` means on this platform; the host sets it (Ctrl by default).
void set_primary_modifier(Modifier m) noexcept;
[[nodiscard]] Modifier primary_modifier() noexcept;

struct Chord {
    Key       key = Key::unknown;
    Modifiers mods;

    [[nodiscard]] bool matches(Key k, Modifiers m) const noexcept { return key == k && mods == m; }
    [[nodiscard]] std::string text() const;  // "Ctrl+Shift+K"
    friend bool operator==(const Chord& a, const Chord& b) noexcept { return a.key == b.key && a.mods == b.mods; }
};

// "Ctrl+Shift+K", "Mod+S", "Alt+Down", "Space"
[[nodiscard]] std::optional<Chord> parse_chord(std::string_view text);
// Alternatives: "Right|Up", "Enter|Space"
[[nodiscard]] std::optional<std::vector<Chord>> parse_chords(std::string_view text);

}  // namespace omui
