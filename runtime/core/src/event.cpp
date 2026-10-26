module openmui.core;

import std;

namespace omui {

namespace {

constexpr std::array<std::string_view, 6> pointer_kinds{"down", "up", "move", "enter", "leave", "cancel"};
constexpr std::array<std::string_view, 3> pointer_types{"mouse", "touch", "pen"};
constexpr std::array<std::string_view, 6> buttons{"none", "left", "right", "middle", "back", "forward"};

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
    return std::nullopt;
}

}  // namespace omui
