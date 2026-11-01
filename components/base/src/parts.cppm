// openmui.ui.base:parts — what the base components share: colour mixing,
// the keyboard focus ring, and labels that shape their text once. No motion:
// a component draws its state; how a change moves is its slots' (universal
// motion, D-93), the same for every component.
//
// Exported: theme and component packages build on the same parts.
export module openmui.ui.base:parts;

import std;
import openmui.core;
import openmui.view;

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

// One line (or a wrapped block) of text in a theme text style, shaped when
// its text, style, colour or width changes and not otherwise.
class Label {
public:
    void set_text(std::string text);
    void set_style(std::string token);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] Size measure(Services& services, Color color, float max_width = std::numeric_limits<float>::infinity());
    // Its widest unbreakable piece (a word): narrower than this, it would break inside one.
    [[nodiscard]] float min_content(Services& services, Color color);
    [[nodiscard]] float baseline(Services& services, Color color);
    void paint(PaintContext& ctx, Point origin, Color color);

private:
    typeset::Paragraph& shaped(Services& services, Color color, float width);

    std::string                       text_;
    std::string                       style_ = "label";
    std::optional<typeset::Paragraph> paragraph_;
    Color                             color_{};
    float                             width_ = -1;
    bool                              dirty_ = true;
};

}  // namespace omui::parts
