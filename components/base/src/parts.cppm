// openmui.ui.base:parts — what the base components share: motion of a
// component's own scalars on the runtime's animation set, colour mixing, the
// keyboard focus ring, and labels that shape their text once.
//
// Exported: theme and component packages build on the same parts.
export module openmui.ui.base:parts;

import std;
import openmui.core;
import openmui.view;

export namespace omui::parts {

// A scalar a component animates itself (a thumb's place, a press, a colour
// mix), on the runtime's clock. Painting reads it; while it moves the
// component asks for frames (`keep_frames`).
class Motion {
public:
    Motion() = default;
    Motion(const Motion&) = delete;
    Motion& operator=(const Motion&) = delete;
    ~Motion();

    void attach(Services& services, double initial);
    [[nodiscard]] bool attached() const noexcept { return services_ != nullptr; }
    // Toward `target` as `transition` moves (a theme motion token's tween or spring).
    void to(double target, const Transition& transition);
    void jump(double value);
    [[nodiscard]] double value() const;
    [[nodiscard]] double target() const noexcept { return target_; }
    [[nodiscard]] bool moving() const;

private:
    Services*          services_ = nullptr;
    anim::Set::Channel channel_;
    double             target_ = 0;
};

// Keeps frames and paints coming while any of `motions` moves.
void keep_frames(PaintContext& ctx, std::initializer_list<const Motion*> motions);

[[nodiscard]] constexpr Color mix(Color a, Color b, float t) noexcept {
    const float u = 1 - t;
    return {a.r * u + b.r * t, a.g * u + b.g * t, a.b * u + b.b * t, a.a * u + b.a * t};
}

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
