// omui.ui.base:parts — what the base components share: colour mixing,
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

// A view box's text ("x y width height"): none unless four numbers, the width and the height above zero.
[[nodiscard]] std::optional<Rect> view_box(std::string_view text) noexcept;
// A view box laid into `into` as a path lays its data: its ratio kept, centred.
[[nodiscard]] mlibs::geom::Affine fitted(const Rect& view_box, const Rect& into) noexcept;
// SVG path data as a path to draw (a path's shape, a link's icon); none where it does not parse or draws nothing.
[[nodiscard]] std::optional<mrender::Path> shape_of(std::string_view data);

// A small downward chevron (selects, menus), centred on `c`.
void chevron(PaintContext& ctx, Point c, float size, Color color);

// The ring keyboard focus draws around a control (theme token `focus`).
void focus_ring(PaintContext& ctx, const Rect& r, const CornerRadii& radii);

// Where an agent acts (agent presence, C2): four corners round `box`, each
// arm `arm` long — the one mark of an agent's attention, wherever it is
// drawn (the presence over a page, the promo's agent, a game's intent).
void agent_corners(mrender::DisplayList& list, const Rect& box, Color ink, float arm, float width = 2);

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

// A pressed control is this much smaller, about its centre (the component checklist's B4: buttons, checkboxes).
inline constexpr float press_scale = 0.03f;

// While it lives, what is drawn is scaled about `box`'s centre by 1 − press_scale × `pressed` (0 … 1, a little past
// either end as a spring overshoots); undone when it goes. Nothing is pushed when nothing is pressed.
class Pressed {
public:
    Pressed(PaintContext& ctx, const Rect& box, float pressed);
    ~Pressed();
    Pressed(const Pressed&) = delete;
    Pressed& operator=(const Pressed&) = delete;

private:
    PaintContext& ctx_;
    bool          pushed_ = false;
};

// How far something dragged `past` its end (signed, px) is shown past it: damped, approaching but never reaching
// `limit` (a rubber band, as iOS draws an overscroll; the component checklist's B4). Continuous, odd, and 1 : 1 at
// first: limit × (1 − 1 / (1 + |past| / limit)).
[[nodiscard]] float rubber_band(float past, float limit) noexcept;

// Where a dragged thumb is drawn, between steps: at `raw` (a fraction of the track), pulled to `snapped` (the step
// the value is on) the more the nearer it is — held at a tick, following the pointer between (magnetic, B4).
// `half` is half a step as a fraction; the value itself is always `snapped`.
[[nodiscard]] double magnetic(double raw, double snapped, double half) noexcept;

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
    // Its lines as last painted, for a node at `node_at` in the window: what a component says of its text
    // (`text_lines`), so the page's own text over a canvas stands on the words drawn. None before it was painted.
    [[nodiscard]] std::vector<TextLineBox> lines(Point node_at) const;

private:
    mlibs::text::Paragraph& shaped(Services& services, Color color, float width);

    std::string                       text_;
    std::string                       style_ = "label";
    int                               max_lines_ = 0;
    std::optional<mlibs::text::Paragraph> paragraph_;
    Color                             color_{};
    float                             width_ = -1;
    bool                              dirty_ = true;
    std::optional<Point>              painted_at_;  // in its node, where it was last painted
};

}  // namespace omui::parts
