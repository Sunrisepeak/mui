module openmui.core;

import std;

namespace omui {

namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(Key::count_)> key_names{
    "Unknown",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W",
    "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Enter", "Escape", "Backspace", "Tab", "Space",
    "Minus", "Equal", "BracketLeft", "BracketRight", "Backslash", "Semicolon", "Quote", "Backquote", "Comma", "Period",
    "Slash",
    "Insert", "Delete", "Home", "End", "PageUp", "PageDown",
    "Left", "Right", "Up", "Down",
    "CapsLock", "PrintScreen", "ScrollLock", "Pause", "ContextMenu",
    "Shift", "Control", "Alt", "Meta",
};

static_assert(!key_names.back().empty(), "a name for every key");

std::atomic<std::uint8_t> primary{static_cast<std::uint8_t>(Modifier::ctrl)};

bool iequal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

std::string_view name(Key k) noexcept {
    auto i = static_cast<std::size_t>(k);
    return i < key_names.size() ? key_names[i] : "Unknown";
}

std::optional<Key> parse_key(std::string_view text) noexcept {
    for (std::size_t i = 1; i < key_names.size(); ++i)
        if (iequal(key_names[i], text)) return static_cast<Key>(i);
    // Common alternative spellings.
    if (iequal(text, "Esc")) return Key::escape;
    if (iequal(text, "Return")) return Key::enter;
    if (iequal(text, "Del")) return Key::delete_;
    if (iequal(text, "ArrowLeft")) return Key::left;
    if (iequal(text, "ArrowRight")) return Key::right;
    if (iequal(text, "ArrowUp")) return Key::up;
    if (iequal(text, "ArrowDown")) return Key::down;
    if (iequal(text, "PgUp")) return Key::page_up;
    if (iequal(text, "PgDn")) return Key::page_down;
    return std::nullopt;
}

void set_primary_modifier(Modifier m) noexcept { primary.store(static_cast<std::uint8_t>(m)); }
Modifier primary_modifier() noexcept { return static_cast<Modifier>(primary.load()); }

std::string Chord::text() const {
    std::string out;
    if (mods.has(Modifier::ctrl)) out += "Ctrl+";
    if (mods.has(Modifier::alt)) out += "Alt+";
    if (mods.has(Modifier::shift)) out += "Shift+";
    if (mods.has(Modifier::meta)) out += "Meta+";
    out += name(key);
    return out;
}

std::optional<Chord> parse_chord(std::string_view text) {
    Chord c;
    while (true) {
        auto plus = text.find('+');
        // A trailing "+" names the plus key itself is not supported; "Shift+Equal" is.
        std::string_view part = text.substr(0, plus);
        if (plus == std::string_view::npos) {
            auto k = parse_key(part);
            if (!k || *k == Key::unknown) return std::nullopt;
            c.key = *k;
            return c;
        }
        if (iequal(part, "Ctrl") || iequal(part, "Control")) c.mods = c.mods | Modifier::ctrl;
        else if (iequal(part, "Alt") || iequal(part, "Option")) c.mods = c.mods | Modifier::alt;
        else if (iequal(part, "Shift")) c.mods = c.mods | Modifier::shift;
        else if (iequal(part, "Meta") || iequal(part, "Cmd") || iequal(part, "Super")) c.mods = c.mods | Modifier::meta;
        else if (iequal(part, "Mod")) c.mods = c.mods | primary_modifier();
        else return std::nullopt;
        text.remove_prefix(plus + 1);
    }
}

std::optional<std::vector<Chord>> parse_chords(std::string_view text) {
    std::vector<Chord> out;
    while (true) {
        auto bar = text.find('|');
        auto c = parse_chord(text.substr(0, bar));
        if (!c) return std::nullopt;
        out.push_back(*c);
        if (bar == std::string_view::npos) return out;
        text.remove_prefix(bar + 1);
    }
}

}  // namespace omui
