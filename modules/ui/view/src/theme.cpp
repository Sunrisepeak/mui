module openmui.view;

import std;

namespace omui {

Color Theme::color(std::string_view token, Color fallback) const {
    if (token.empty()) return fallback;
    if (token.front() == '#') return spec::Color::parse(token).transform([](spec::Color c) { return Color{c.r, c.g, c.b, c.a}; }).value_or(fallback);
    auto it = colors.find(token);
    return it == colors.end() ? fallback : it->second;
}

const TextToken& Theme::text_style(std::string_view token) const {
    auto it = text.find(token);
    if (it != text.end()) return it->second;
    static const TextToken body{};
    auto b = text.find("body");
    return b == text.end() ? body : b->second;
}

float Theme::space(std::string_view token, float fallback) const {
    auto it = spacing.find(token);
    return it == spacing.end() ? fallback : it->second;
}

float Theme::corner(std::string_view token, float fallback) const {
    auto it = radius.find(token);
    return it == radius.end() ? fallback : it->second;
}

Transition Theme::transition(std::string_view token) const {
    auto it = motion.find(token);
    if (it != motion.end()) return it->second;
    return Transition::token(token).value_or(Transition::tween(hanim::seconds(0.2)));
}

namespace {

void common(Theme& t) {
    t.text["display"] = {40, 700, 1.15f, -0.5f, {"Inter"}};
    t.text["title"] = {28, 650, 1.2f, -0.3f, {"Inter"}};
    t.text["heading"] = {20, 600, 1.3f, -0.1f, {"Inter"}};
    t.text["body"] = {15, 400, 1.45f, 0, {"Inter"}};
    t.text["label"] = {14, 500, 1.3f, 0, {"Inter"}};
    t.text["caption"] = {12, 400, 1.35f, 0.1f, {"Inter"}};
    t.text["code"] = {13, 400, 1.5f, 0, {"JetBrains Mono"}};
    for (auto [k, v] : {std::pair{"none", 0.0f}, {"xs", 4.0f}, {"s", 8.0f}, {"m", 12.0f}, {"l", 16.0f}, {"xl", 24.0f}, {"xxl", 32.0f}})
        t.spacing[k] = v;
    for (auto [k, v] : {std::pair{"none", 0.0f}, {"small", 4.0f}, {"medium", 8.0f}, {"large", 12.0f}, {"xl", 16.0f}, {"full", 9999.0f}})
        t.radius[k] = v;
    for (const auto& token : spec::motion_tokens)
        if (auto tr = Transition::token(token.name)) t.motion[std::string(token.name)] = *tr;
}

}  // namespace

Theme Theme::light() {
    Theme t;
    t.name = "light";
    t.dark = false;
    auto c = [&](std::string_view k, std::uint32_t hex, float a = 1) { t.colors[std::string(k)] = Color::hex(hex, a); };
    c("background", 0xf8fafc);
    c("surface", 0xffffff);
    c("surface_variant", 0xf1f5f9);
    c("surface_pressed", 0xe2e8f0);
    c("text", 0x0f172a);
    c("text_muted", 0x64748b);
    c("text_disabled", 0x94a3b8);
    c("border", 0xcbd5e1);
    c("border_strong", 0x94a3b8);
    c("accent", 0x4f46e5);
    c("accent_hover", 0x4338ca);
    c("accent_pressed", 0x3730a3);
    c("on_accent", 0xffffff);
    c("accent_subtle", 0xe0e7ff);
    c("focus", 0x6366f1, 0.55f);
    c("danger", 0xdc2626);
    c("on_danger", 0xffffff);
    c("success", 0x16a34a);
    c("warning", 0xd97706);
    c("selection", 0x6366f1, 0.25f);
    c("scrim", 0x0f172a, 0.45f);
    c("track", 0xe2e8f0);
    c("thumb", 0xffffff);
    c("tooltip", 0x0f172a);
    c("on_tooltip", 0xf8fafc);
    t.shadows["sm"] = {{0, 1}, 2, 0, Color::hex(0x0f172a, 0.08f)};
    t.shadows["md"] = {{0, 4}, 12, -2, Color::hex(0x0f172a, 0.12f)};
    t.shadows["lg"] = {{0, 12}, 32, -4, Color::hex(0x0f172a, 0.18f)};
    common(t);
    return t;
}

Theme Theme::dark_theme() {
    Theme t = light();
    t.name = "dark";
    t.dark = true;
    auto c = [&](std::string_view k, std::uint32_t hex, float a = 1) { t.colors[std::string(k)] = Color::hex(hex, a); };
    c("background", 0x0b1120);
    c("surface", 0x111827);
    c("surface_variant", 0x1f2937);
    c("surface_pressed", 0x374151);
    c("text", 0xf1f5f9);
    c("text_muted", 0x94a3b8);
    c("text_disabled", 0x475569);
    c("border", 0x334155);
    c("border_strong", 0x475569);
    c("accent", 0x818cf8);
    c("accent_hover", 0x6366f1);
    c("accent_pressed", 0x4f46e5);
    c("on_accent", 0x0b1120);
    c("accent_subtle", 0x312e81);
    c("track", 0x334155);
    c("thumb", 0xf1f5f9);
    c("tooltip", 0xf1f5f9);
    c("on_tooltip", 0x0b1120);
    return t;
}

}  // namespace omui
