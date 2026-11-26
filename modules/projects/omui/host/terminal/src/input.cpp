// A terminal's input bytes decoded: control characters, CSI and SS3 sequences with xterm's modifier parameter,
// SGR mouse reports, bracketed paste, focus reports and UTF-8 text.
module omui.host.terminal;

import std;
import omui.core;

namespace omui::terminal {

namespace {

constexpr char escape = '\x1b';

// The key a printable ASCII character is on (a US layout: what a terminal sends gives no more), and whether it
// took Shift.
std::pair<Key, bool> key_of(char c) {
    if (c >= 'a' && c <= 'z') return {static_cast<Key>(static_cast<int>(Key::a) + (c - 'a')), false};
    if (c >= 'A' && c <= 'Z') return {static_cast<Key>(static_cast<int>(Key::a) + (c - 'A')), true};
    if (c >= '0' && c <= '9') return {static_cast<Key>(static_cast<int>(Key::digit0) + (c - '0')), false};
    switch (c) {
        case ' ': return {Key::space, false};
        case '-': return {Key::minus, false};
        case '=': return {Key::equal, false};
        case '[': return {Key::left_bracket, false};
        case ']': return {Key::right_bracket, false};
        case '\\': return {Key::backslash, false};
        case ';': return {Key::semicolon, false};
        case '\'': return {Key::apostrophe, false};
        case '`': return {Key::grave, false};
        case ',': return {Key::comma, false};
        case '.': return {Key::period, false};
        case '/': return {Key::slash, false};
        default: return {Key::unknown, false};
    }
}

void press(std::vector<InputEvent>& out, Key key, Modifiers mods) {
    KeyEvent down{.kind = KeyKind::down, .key = key, .mods = mods};
    KeyEvent up = down;
    up.kind = KeyKind::up;
    out.push_back(down);
    out.push_back(up);
}

// xterm's modifier parameter: 1 + shift 1, alt 2, ctrl 4, meta 8.
Modifiers modifiers_of(int parameter) {
    Modifiers mods;
    const int bits = std::max(parameter - 1, 0);
    if (bits & 1) mods = mods | Modifier::shift;
    if (bits & 2) mods = mods | Modifier::alt;
    if (bits & 4) mods = mods | Modifier::ctrl;
    if (bits & 8) mods = mods | Modifier::meta;
    return mods;
}

std::vector<int> numbers(std::string_view text) {
    std::vector<int> out;
    int value = 0;
    bool any = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            any = true;
        } else if (c == ';' || c == ':') {
            out.push_back(any ? value : 0);
            value = 0;
            any = false;
        }
    }
    out.push_back(any ? value : 0);
    return out;
}

// How many bytes a UTF-8 sequence that starts with `lead` has (1 for a byte that starts none).
std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string normalised_lines(std::string text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

}  // namespace

std::vector<InputEvent> Input::feed(std::string_view bytes) {
    kept_.append(bytes);
    std::vector<InputEvent> out;
    std::size_t at = 0;
    while (at < kept_.size()) {
        const std::size_t taken = decode(std::string_view(kept_).substr(at), out);
        if (taken == 0) break;
        at += taken;
    }
    kept_.erase(0, at);
    return out;
}

std::vector<InputEvent> Input::flush() {
    std::vector<InputEvent> out;
    if (kept_.empty() || in_paste_) return out;
    if (kept_.front() == escape) {
        // Nothing completed it: the Escape key, and what followed it as it is.
        press(out, Key::escape, {});
        const std::string rest = kept_.substr(1);
        kept_.clear();
        std::vector<InputEvent> more = feed(rest);
        out.insert(out.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
        kept_.clear();
        return out;
    }
    kept_.clear();  // a UTF-8 sequence cut short and never ended: dropped
    return out;
}

std::size_t Input::decode(std::string_view bytes, std::vector<InputEvent>& out) {
    if (in_paste_) {
        static constexpr std::string_view end = "\x1b[201~";
        if (const std::size_t found = bytes.find(end); found != std::string_view::npos) {
            pasting_.append(bytes.substr(0, found));
            out.push_back(TextEvent{normalised_lines(std::move(pasting_))});
            pasting_.clear();
            in_paste_ = false;
            return found + end.size();
        }
        // Keep what may be the start of the end; the rest is the paste's.
        const std::size_t safe = bytes.size() > end.size() ? bytes.size() - end.size() : 0;
        pasting_.append(bytes.substr(0, safe));
        return safe;
    }
    const auto lead = static_cast<unsigned char>(bytes.front());
    if (lead == static_cast<unsigned char>(escape)) {
        if (bytes.size() == 1) return 0;  // an Escape alone, or a sequence not here yet: `flush` decides
        if (bytes[1] == '[') return csi(bytes, out);
        if (bytes[1] == 'O') {  // SS3: F1–F4 and the application-mode cursor keys
            if (bytes.size() < 3) return 0;
            switch (bytes[2]) {
                case 'P': press(out, Key::f1, {}); break;
                case 'Q': press(out, Key::f2, {}); break;
                case 'R': press(out, Key::f3, {}); break;
                case 'S': press(out, Key::f4, {}); break;
                case 'A': press(out, Key::up, {}); break;
                case 'B': press(out, Key::down, {}); break;
                case 'C': press(out, Key::right, {}); break;
                case 'D': press(out, Key::left, {}); break;
                case 'H': press(out, Key::home, {}); break;
                case 'F': press(out, Key::end, {}); break;
                default: break;
            }
            return 3;
        }
        if (bytes[1] == escape) {
            press(out, Key::escape, {});
            return 1;
        }
        // Alt and a key: ESC before it.
        std::vector<InputEvent> inner;
        const std::size_t taken = decode(bytes.substr(1), inner);
        if (taken == 0) return 0;
        for (InputEvent& event : inner)
            if (auto* key = std::get_if<KeyEvent>(&event)) {
                key->mods = key->mods | Modifier::alt;
                out.push_back(*key);
            }
        return taken + 1;
    }
    switch (lead) {
        case '\r':
        case '\n': press(out, Key::enter, {}); return 1;
        case '\t': press(out, Key::tab, {}); return 1;
        case 0x7F:
        case 0x08: press(out, Key::backspace, {}); return 1;
        case 0x03: interrupted_ = true; return 1;  // Ctrl+C: the program ends (the host's to do)
        case 0x00: press(out, Key::space, Modifier::ctrl); return 1;
        default: break;
    }
    if (lead >= 0x01 && lead <= 0x1A) {  // Ctrl and a letter
        press(out, static_cast<Key>(static_cast<int>(Key::a) + (lead - 1)), Modifier::ctrl);
        return 1;
    }
    if (lead < 0x20) return 1;  // the rest of C0 says nothing a key does here
    const std::size_t length = utf8_length(lead);
    if (bytes.size() < length) return 0;
    const std::string text(bytes.substr(0, length));
    if (length == 1) {
        const auto [key, shifted] = key_of(static_cast<char>(lead));
        KeyEvent down{.kind = KeyKind::down, .key = key, .mods = shifted ? Modifiers(Modifier::shift) : Modifiers{}};
        KeyEvent up = down;
        up.kind = KeyKind::up;
        out.push_back(down);
        out.push_back(TextEvent{text});
        out.push_back(up);
    } else {
        out.push_back(TextEvent{text});
    }
    return length;
}

std::size_t Input::csi(std::string_view bytes, std::vector<InputEvent>& out) {
    // Parameters 0x30–0x3F, intermediates 0x20–0x2F, then the final byte 0x40–0x7E.
    std::size_t end = 2;
    while (end < bytes.size() && !(bytes[end] >= 0x40 && bytes[end] <= 0x7E)) ++end;
    if (end >= bytes.size()) return bytes.size() > 64 ? 2 : 0;  // not ended yet (or not a sequence at all)
    const std::string_view sequence = bytes.substr(0, end + 1), parameters = bytes.substr(2, end - 2);
    const char final = bytes[end];
    if (parameters.starts_with('<') && (final == 'M' || final == 'm')) return mouse(sequence, out);
    if (final == '~' && parameters == "200") {
        in_paste_ = true;
        return sequence.size();
    }
    if (parameters.empty() && final == 'I') {
        out.push_back(FocusEvent{true});
        return sequence.size();
    }
    if (parameters.empty() && final == 'O') {
        out.push_back(FocusEvent{false});
        return sequence.size();
    }
    const std::vector<int> values = numbers(parameters);
    const Modifiers mods = values.size() > 1 ? modifiers_of(values[1]) : Modifiers{};
    switch (final) {
        case 'A': press(out, Key::up, mods); break;
        case 'B': press(out, Key::down, mods); break;
        case 'C': press(out, Key::right, mods); break;
        case 'D': press(out, Key::left, mods); break;
        case 'H': press(out, Key::home, mods); break;
        case 'F': press(out, Key::end, mods); break;
        case 'Z': press(out, Key::tab, Modifier::shift); break;
        case 'P': press(out, Key::f1, mods); break;
        case 'Q': press(out, Key::f2, mods); break;
        case 'R': press(out, Key::f3, mods); break;
        case 'S': press(out, Key::f4, mods); break;
        case '~': {
            static constexpr std::array<std::pair<int, Key>, 18> tilde{{{1, Key::home},   {2, Key::insert},  {3, Key::delete_}, {4, Key::end},
                                                                        {5, Key::page_up}, {6, Key::page_down}, {7, Key::home}, {8, Key::end},
                                                                        {15, Key::f5},    {17, Key::f6},     {18, Key::f7},     {19, Key::f8},
                                                                        {20, Key::f9},    {21, Key::f10},    {23, Key::f11},    {24, Key::f12},
                                                                        {11, Key::f1},    {12, Key::f2}}};
            for (const auto& [number, key] : tilde)
                if (!values.empty() && values[0] == number) press(out, key, mods);
            break;
        }
        default: break;  // a report this host did not ask for
    }
    return sequence.size();
}

std::size_t Input::mouse(std::string_view sequence, std::vector<InputEvent>& out) {
    const std::vector<int> values = numbers(sequence.substr(3, sequence.size() - 4));
    if (values.size() < 3) return sequence.size();
    const int code = values[0];
    const Point at{(static_cast<float>(values[1] - 1) + 0.5f) * cell_.column, (static_cast<float>(values[2] - 1) + 0.5f) * cell_.row};
    Modifiers mods;
    if (code & 4) mods = mods | Modifier::shift;
    if (code & 8) mods = mods | Modifier::alt;
    if (code & 16) mods = mods | Modifier::ctrl;
    if (code & 64) {  // the wheel: up, down, left, right; three rows a notch
        const float step = 3 * cell_.row;
        const Point delta = (code & 3) == 0 ? Point{0, -step} : (code & 3) == 1 ? Point{0, step} : (code & 3) == 2 ? Point{-step, 0} : Point{step, 0};
        out.push_back(WheelEvent{.position = at, .delta = delta, .mods = mods});
        return sequence.size();
    }
    const MouseButton button = (code & 3) == 0 ? MouseButton::left : (code & 3) == 1 ? MouseButton::middle : (code & 3) == 2 ? MouseButton::right : MouseButton::none;
    const std::uint32_t bit = button == MouseButton::none ? 0 : 1u << (static_cast<int>(button) - 1);
    PointerEvent event{.kind = PointerKind::move, .position = at, .mods = mods};
    if (code & 32) {  // motion, a button held or none
        event.buttons = buttons_;
        out.push_back(event);
        return sequence.size();
    }
    // A press or a release where the pointer is: moved there first, as a window's pointer would have.
    PointerEvent moved = event;
    moved.buttons = buttons_;
    out.push_back(moved);
    event.button = button;
    if (sequence.back() == 'M') {
        buttons_ |= bit;
        event.kind = PointerKind::down;
    } else {
        buttons_ &= ~bit;
        event.kind = PointerKind::up;
    }
    event.buttons = buttons_;
    out.push_back(event);
    return sequence.size();
}

}  // namespace omui::terminal
