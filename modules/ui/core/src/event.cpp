module openmui.core;

import std;

namespace omui {

namespace {

constexpr std::array<std::string_view, 6> pointer_kinds{"down", "up", "move", "enter", "leave", "cancel"};
constexpr std::array<std::string_view, 3> pointer_types{"mouse", "touch", "pen"};
constexpr std::array<std::string_view, 6> buttons{"none", "left", "right", "middle", "back", "forward"};
constexpr std::array<std::string_view, gamepad_buttons> pad_buttons{
    "south", "east", "west", "north", "back", "guide", "start", "leftStick", "rightStick", "leftShoulder", "rightShoulder",
    "leftTrigger", "rightTrigger", "dpadUp", "dpadDown", "dpadLeft", "dpadRight"};
constexpr std::array<std::string_view, gamepad_axes> pad_axes{"leftX", "leftY", "rightX", "rightY", "leftTrigger", "rightTrigger"};
constexpr std::array<std::string_view, 5> pad_kinds{"connected", "disconnected", "down", "up", "axis"};

template <std::size_t N>
std::optional<std::size_t> index_of(const std::array<std::string_view, N>& names, std::string_view s) {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == s) return i;
    return std::nullopt;
}

Value mods_value(Modifiers m) {
    Value::List out;
    if (m.has(Modifier::shift)) out.emplace_back("shift");
    if (m.has(Modifier::ctrl)) out.emplace_back("ctrl");
    if (m.has(Modifier::alt)) out.emplace_back("alt");
    if (m.has(Modifier::meta)) out.emplace_back("meta");
    return Value(std::move(out));
}

Modifiers mods_from(const Value* v) {
    Modifiers m;
    if (!v || !v->list()) return m;
    for (const Value& e : *v->list()) {
        auto s = e.as_string();
        if (s == "shift") m = m | Modifier::shift;
        else if (s == "ctrl") m = m | Modifier::ctrl;
        else if (s == "alt") m = m | Modifier::alt;
        else if (s == "meta") m = m | Modifier::meta;
    }
    return m;
}

float num(const Value& v, std::string_view key, float fallback = 0) {
    const Value* x = v.get(key);
    return x ? static_cast<float>(x->as_double(fallback)) : fallback;
}

}  // namespace

Value to_value(const InputEvent& e) {
    Value v;
    std::visit(
        [&](const auto& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::same_as<T, PointerEvent>) {
                v.set("type", "pointer").set("kind", pointer_kinds[static_cast<std::size_t>(ev.kind)]);
                v.set("x", ev.position.x).set("y", ev.position.y);
                if (ev.pointer) v.set("pointer", ev.pointer);
                if (ev.type != PointerType::mouse) v.set("pointerType", pointer_types[static_cast<std::size_t>(ev.type)]);
                if (ev.button != MouseButton::none) v.set("button", buttons[static_cast<std::size_t>(ev.button)]);
                if (ev.buttons) v.set("buttons", ev.buttons);
                if (ev.mods.bits) v.set("mods", mods_value(ev.mods));
                if (ev.clicks != 1) v.set("clicks", static_cast<int>(ev.clicks));
            } else if constexpr (std::same_as<T, WheelEvent>) {
                v.set("type", "wheel").set("x", ev.position.x).set("y", ev.position.y);
                v.set("dx", ev.delta.x).set("dy", ev.delta.y);
                if (ev.precise) v.set("precise", true);
                if (ev.mods.bits) v.set("mods", mods_value(ev.mods));
            } else if constexpr (std::same_as<T, KeyEvent>) {
                v.set("type", "key").set("kind", ev.kind == KeyKind::down ? "down" : "up").set("key", name(ev.key));
                if (ev.mods.bits) v.set("mods", mods_value(ev.mods));
                if (ev.repeat) v.set("repeat", true);
            } else if constexpr (std::same_as<T, TextEvent>) {
                v.set("type", "text").set("text", ev.text);
            } else if constexpr (std::same_as<T, PreeditEvent>) {
                v.set("type", "preedit").set("text", ev.text);
                if (ev.cursor_begin >= 0) v.set("cursorBegin", ev.cursor_begin).set("cursorEnd", ev.cursor_end);
            } else if constexpr (std::same_as<T, FocusEvent>) {
                v.set("type", "focus").set("gained", ev.gained);
            } else if constexpr (std::same_as<T, ResizeEvent>) {
                v.set("type", "resize").set("width", ev.size.width).set("height", ev.size.height).set("scale", ev.scale);
            } else if constexpr (std::same_as<T, CloseRequestEvent>) {
                v.set("type", "close");
            } else if constexpr (std::same_as<T, GamepadEvent>) {
                v.set("type", "gamepad").set("kind", pad_kinds[static_cast<std::size_t>(ev.kind)]).set("device", ev.device);
                if (ev.kind == GamepadKind::down || ev.kind == GamepadKind::up) v.set("button", name(ev.button));
                if (ev.kind == GamepadKind::axis) v.set("axis", name(ev.axis)).set("value", ev.value);
                if (ev.kind == GamepadKind::connected && !ev.name.empty()) v.set("name", ev.name);
            }
        },
        e);
    return v;
}

std::optional<InputEvent> event_from_value(const Value& v) {
    const Value* type = v.get("type");
    if (!type) return std::nullopt;
    auto t = type->as_string();
    if (t == "pointer") {
        PointerEvent p;
        auto kind = index_of(pointer_kinds, v.get("kind") ? v.get("kind")->as_string() : "move");
        if (!kind) return std::nullopt;
        p.kind = static_cast<PointerKind>(*kind);
        p.position = {num(v, "x"), num(v, "y")};
        if (const Value* x = v.get("pointer")) p.pointer = static_cast<std::uint32_t>(x->as_int());
        if (const Value* x = v.get("pointerType"))
            if (auto i = index_of(pointer_types, x->as_string())) p.type = static_cast<PointerType>(*i);
        if (const Value* x = v.get("button"))
            if (auto i = index_of(buttons, x->as_string())) p.button = static_cast<MouseButton>(*i);
        if (const Value* x = v.get("buttons")) p.buttons = static_cast<std::uint32_t>(x->as_int());
        p.mods = mods_from(v.get("mods"));
        if (const Value* x = v.get("clicks")) p.clicks = static_cast<std::uint8_t>(x->as_int(1));
        return p;
    }
    if (t == "wheel") {
        WheelEvent w;
        w.position = {num(v, "x"), num(v, "y")};
        w.delta = {num(v, "dx"), num(v, "dy")};
        if (const Value* x = v.get("precise")) w.precise = x->as_bool();
        w.mods = mods_from(v.get("mods"));
        return w;
    }
    if (t == "key") {
        KeyEvent k;
        k.kind = v.get("kind") && v.get("kind")->as_string() == "up" ? KeyKind::up : KeyKind::down;
        auto key = parse_key(v.get("key") ? v.get("key")->as_string() : "");
        if (!key) return std::nullopt;
        k.key = *key;
        k.mods = mods_from(v.get("mods"));
        if (const Value* x = v.get("repeat")) k.repeat = x->as_bool();
        return k;
    }
    if (t == "text") return TextEvent{std::string(v.get("text") ? v.get("text")->as_string() : "")};
    if (t == "preedit") {
        PreeditEvent p;
        p.text = std::string(v.get("text") ? v.get("text")->as_string() : "");
        if (const Value* x = v.get("cursorBegin")) p.cursor_begin = static_cast<std::int32_t>(x->as_int(-1));
        if (const Value* x = v.get("cursorEnd")) p.cursor_end = static_cast<std::int32_t>(x->as_int(-1));
        return p;
    }
    if (t == "focus") return FocusEvent{v.get("gained") ? v.get("gained")->as_bool(true) : true};
    if (t == "resize") return ResizeEvent{{num(v, "width"), num(v, "height")}, num(v, "scale", 1.0f)};
    if (t == "close") return CloseRequestEvent{};
    if (t == "gamepad") {
        GamepadEvent g;
        auto kind = index_of(pad_kinds, v.get("kind") ? v.get("kind")->as_string() : "");
        if (!kind) return std::nullopt;
        g.kind = static_cast<GamepadKind>(*kind);
        if (const Value* x = v.get("device")) g.device = static_cast<std::uint32_t>(x->as_int());
        if (g.kind == GamepadKind::down || g.kind == GamepadKind::up) {
            auto b = parse_gamepad_button(v.get("button") ? v.get("button")->as_string() : "");
            if (!b) return std::nullopt;
            g.button = *b;
        }
        if (g.kind == GamepadKind::axis) {
            auto a = parse_gamepad_axis(v.get("axis") ? v.get("axis")->as_string() : "");
            if (!a) return std::nullopt;
            g.axis = *a;
            g.value = num(v, "value");
        }
        if (const Value* x = v.get("name")) g.name = std::string(x->as_string());
        return g;
    }
    return std::nullopt;
}

std::string_view name(GamepadButton b) noexcept {
    const auto i = static_cast<std::size_t>(b);
    return i < pad_buttons.size() ? pad_buttons[i] : std::string_view();
}

std::string_view name(GamepadAxis a) noexcept {
    const auto i = static_cast<std::size_t>(a);
    return i < pad_axes.size() ? pad_axes[i] : std::string_view();
}

std::optional<GamepadButton> parse_gamepad_button(std::string_view text) noexcept {
    if (auto i = index_of(pad_buttons, text)) return static_cast<GamepadButton>(*i);
    return std::nullopt;
}

std::optional<GamepadAxis> parse_gamepad_axis(std::string_view text) noexcept {
    if (auto i = index_of(pad_axes, text)) return static_cast<GamepadAxis>(*i);
    return std::nullopt;
}

// ── GamepadReader ──

namespace {

// Steps of 1/256: exact in binary, so a value survives its JSON form.
float quantise(float v) { return std::round(std::clamp(v, -1.0f, 1.0f) * 256.0f) / 256.0f; }

// A reading past the dead zone, stretched so the zone's edge is 0 and full is 1.
float past(float magnitude, float zone) { return magnitude <= zone ? 0.0f : std::min(1.0f, (magnitude - zone) / (1.0f - zone)); }

constexpr bool is_trigger(GamepadAxis a) { return a == GamepadAxis::left_trigger || a == GamepadAxis::right_trigger; }

}  // namespace

GamepadReader::Device* GamepadReader::find(std::uint32_t device) noexcept {
    for (Device& d : devices_)
        if (d.id == device) return &d;
    return nullptr;
}

bool GamepadReader::connected(std::uint32_t device) const noexcept {
    return std::ranges::any_of(devices_, [&](const Device& d) { return d.id == device; });
}

std::vector<std::uint32_t> GamepadReader::devices() const {
    std::vector<std::uint32_t> out;
    for (const Device& d : devices_) out.push_back(d.id);
    return out;
}

void GamepadReader::connect(std::uint32_t device, std::string_view name, std::vector<InputEvent>& out) {
    if (find(device)) return;
    devices_.push_back({.id = device});
    out.push_back(GamepadEvent{.kind = GamepadKind::connected, .device = device, .name = std::string(name)});
}

void GamepadReader::disconnect(std::uint32_t device, std::vector<InputEvent>& out) {
    Device* d = find(device);
    if (!d) return;
    for (std::size_t b = 0; b < gamepad_buttons; ++b)
        if (d->held[b]) out.push_back(GamepadEvent{.kind = GamepadKind::up, .device = device, .button = static_cast<GamepadButton>(b)});
    for (std::size_t a = 0; a < gamepad_axes; ++a)
        if (d->value[a] != 0) out.push_back(GamepadEvent{.kind = GamepadKind::axis, .device = device, .axis = static_cast<GamepadAxis>(a)});
    out.push_back(GamepadEvent{.kind = GamepadKind::disconnected, .device = device});
    std::erase_if(devices_, [&](const Device& x) { return x.id == device; });
}

void GamepadReader::button(std::uint32_t device, GamepadButton b, bool down, std::vector<InputEvent>& out) {
    Device* d = find(device);
    const auto i = static_cast<std::size_t>(b);
    if (!d || i >= gamepad_buttons || d->held[i] == down) return;
    d->held[i] = down;
    out.push_back(GamepadEvent{.kind = down ? GamepadKind::down : GamepadKind::up, .device = device, .button = b});
}

void GamepadReader::emit_axis(Device& d, GamepadAxis a, float v, std::vector<InputEvent>& out) {
    const auto i = static_cast<std::size_t>(a);
    v = quantise(v);
    if (d.value[i] == v) return;
    d.value[i] = v;
    out.push_back(GamepadEvent{.kind = GamepadKind::axis, .device = d.id, .axis = a, .value = v});
}

void GamepadReader::axis(std::uint32_t device, GamepadAxis a, float raw, std::vector<InputEvent>& out) {
    Device* d = find(device);
    const auto i = static_cast<std::size_t>(a);
    if (!d || i >= gamepad_axes || !std::isfinite(raw)) return;
    d->raw[i] = raw;
    if (is_trigger(a)) {
        const float v = past(std::clamp(raw, 0.0f, 1.0f), trigger_dead_zone);
        emit_axis(*d, a, v, out);
        // The trigger as a button, with a little hysteresis so a held half-pull does not chatter.
        const GamepadButton b = a == GamepadAxis::left_trigger ? GamepadButton::left_trigger : GamepadButton::right_trigger;
        const bool held = d->held[static_cast<std::size_t>(b)];
        if (!held && v >= trigger_press) button(device, b, true, out);
        else if (held && v < trigger_release) button(device, b, false, out);
        return;
    }
    // A stick: the dead zone is a circle, so a diagonal is as easy as a straight push.
    const std::size_t x = i & ~std::size_t{1}, y = x + 1;
    const float rx = std::clamp(d->raw[x], -1.0f, 1.0f), ry = std::clamp(d->raw[y], -1.0f, 1.0f);
    const float m = std::sqrt(rx * rx + ry * ry);
    const float k = m > 0 ? past(m, stick_dead_zone) / m : 0.0f;
    emit_axis(*d, static_cast<GamepadAxis>(x), rx * k, out);
    emit_axis(*d, static_cast<GamepadAxis>(y), ry * k, out);
}

void GamepadReader::update(std::uint32_t device, std::string_view name, const std::array<bool, gamepad_buttons>& buttons,
                           const std::array<float, gamepad_axes>& axes, std::vector<InputEvent>& out) {
    connect(device, name, out);
    for (std::size_t a = 0; a < gamepad_axes; ++a) axis(device, static_cast<GamepadAxis>(a), axes[a], out);
    for (std::size_t b = 0; b < gamepad_buttons; ++b) {
        const auto gb = static_cast<GamepadButton>(b);
        if (gb == GamepadButton::left_trigger || gb == GamepadButton::right_trigger) continue;  // the axes say
        button(device, gb, buttons[b], out);
    }
}

}  // namespace omui
