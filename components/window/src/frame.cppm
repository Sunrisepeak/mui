// openmui.ui.window:frame — the components.
//
// window_frame lays its content out inside its own padding — the shadow's
// reach, and the title bar above — and composes its buttons (caption_button
// nodes, operated as any button: by pointer, keys and agents) over the title
// bar. It follows the window's signals: a maximized or fullscreen window has
// no shadow and no corners, an inactive one a muted title, a native one no
// frame at all.
export module openmui.ui.window:frame;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.ui.base;
import :window_frame_contract;
import :caption_button_contract;

export namespace omui {

class WindowFrame : public WindowFrameContract<WindowFrame> {
public:
    void mounted();
    void prop_changed(WindowFrameProp p);
    void restyle();

    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::vector<Element> compose();
    bool on_event(EventContext& ctx, const InputEvent& e);
    [[nodiscard]] std::optional<WindowArea> window_area(Point local, Size size) const;

    ActResult minimize(ActContext& ctx);
    ActResult toggle_maximize(ActContext& ctx);
    ActResult close(ActContext& ctx);

    // Whether the frame is drawn (the window is framework-drawn), and how
    // far in its edge is from the node's: the shadow's reach, none when the
    // window fills its screen.
    [[nodiscard]] bool drawn() const;
    [[nodiscard]] float inset() const;
    // The title bar, in the node's coordinates (empty when not drawn).
    [[nodiscard]] Rect title_bar() const;
    // What a frame with these properties keeps around the application, as
    // the window is now: the shadow's reach (none when the window fills its
    // screen or shows nothing behind it), and the title bar above.
    [[nodiscard]] static Insets content_insets(const WindowFrameProps& props);

private:
    [[nodiscard]] bool fills_screen() const;
    ActResult run(std::string_view verb);

    Watch                 watch_;
    mutable parts::Label  title_;
    mutable parts::Motion active_;
    Size                  size_{};
};

class CaptionButton : public CaptionButtonContract<CaptionButton> {
public:
    void mounted();
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    ActResult activate(ActContext& ctx);

    // The verb it invokes.
    [[nodiscard]] std::string_view verb() const noexcept;

private:
    mutable parts::Motion hover_;
};

// The framework's frame around an application (D-75): window_frame mounted
// beneath `slot`'s view, as the slot `window`, and the view kept inside it
// as the window changes (its state, framework-drawn or not). Dropping it
// takes the frame away.
class FramedWindow {
public:
    FramedWindow(Runtime& runtime, WindowFrameOptions options, std::string slot = "main");
    ~FramedWindow();
    FramedWindow(const FramedWindow&) = delete;
    FramedWindow& operator=(const FramedWindow&) = delete;

private:
    Runtime*         runtime_;
    std::string      slot_;
    WindowFrameProps props_;
    Watch            watch_;
};

// Registers window_frame and caption_button by name, and their texts in
// English and Chinese (simplified and traditional).
void register_window_components();

}  // namespace omui
