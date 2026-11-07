// openmui.ui.base:parts — what the base components share: colour mixing,
// the keyboard focus ring, and labels that shape their text once. No motion:
// a component draws its state; how a change moves is its slots' (universal
// motion, D-93), the same for every component.
//
// Exported: theme and component packages build on the same parts.
export module omui.ui.base:parts;

import std;
import omui.core;
import omui.view;

export namespace omui::parts {

[[nodiscard]] constexpr Color mix(Color a, Color b, float t) noexcept {
    const float u = 1 - t;
    return {a.r * u + b.r * t, a.g * u + b.g * t, a.b * u + b.b * t, a.a * u + b.a * t};
}

// A node's own id: the last segment of its path.
[[nodiscard]] std::string segment(const Services& services, NodeId node);

// A small downward chevron (selects, menus), centred on `c`.
void chevron(PaintContext& ctx, Point c, float size, Color color);

// The ring keyboard focus draws around a control (theme token `focus`).
void focus_ring(PaintContext& ctx, const Rect& r, const CornerRadii& radii);

// A small ×, centred on `c` (a close or a remove button).
void cross(PaintContext& ctx, Point c, float size, Color color);

// A check mark, `size` wide, centred on `c` (a chosen chip, a step done).
void check_mark(PaintContext& ctx, Point c, float size, Color color, float width = 1.75f);

// The theme's shadow `token` (sm, md, lg) under a box.
void shadow(PaintContext& ctx, std::string_view token, const Rect& box, const CornerRadii& r);

// A control's depth, from the theme: its shadow `token` ("control",
// "thumb"), as the state it is in has it — "<token>:pressed" tighter,
// "<token>:hovered" lifted — when the theme names that; `fallback` (the
// control's own) when the theme names none; tinted by `object`, the colour
// of what casts it.
void depth(PaintContext& ctx, std::string_view token, const Rect& box, const CornerRadii& radii, bool hovered, bool pressed,
           std::optional<ShadowToken> fallback = std::nullopt, Color object = Color::transparent());

// One line (or a wrapped block) of text in a theme text style, shaped when
// its text, style, colour or width changes and not otherwise.
class Label {
public:
    void set_text(std::string text);
    void set_style(std::string token);
    // At most this many lines at the width it is measured at, the last ending in "…" (0: as many as it takes).
    void set_max_lines(int lines);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] Size measure(Services& services, Color color, float max_width = std::numeric_limits<float>::infinity());
    // Its widest unbreakable piece (a word): narrower than this, it would break inside one.
    [[nodiscard]] float min_content(Services& services, Color color);
    [[nodiscard]] float baseline(Services& services, Color color);
    void paint(PaintContext& ctx, Point origin, Color color);

private:
    mlibs::text::Paragraph& shaped(Services& services, Color color, float width);

    std::string                       text_;
    std::string                       style_ = "label";
    int                               max_lines_ = 0;
    std::optional<mlibs::text::Paragraph> paragraph_;
    Color                             color_{};
    float                             width_ = -1;
    bool                              dirty_ = true;
};

}  // namespace omui::parts
