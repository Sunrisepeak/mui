// openmui.ui.canvas:view — the canvas component (canvas-markdown §1).
//
// Each frame it asks the scene what the camera sees (a grid query: the cost
// follows what is on screen, not the scene's size), picks each node's tier by
// its size on screen, and draws: boxes as the renderer's instanced rounded
// rectangles, a kind's own drawing in screen coordinates, and a node large
// enough to use as a live component — its kind's promote() — laid out at its
// world size and shown under the camera's scale, so it does not reflow while
// the camera zooms. The camera pans by dragging, zooms about the cursor (the
// wheel, a pinch, ⌃/⌘ with a trackpad), glides after a fling and moves on
// springs to named views; all of it a function of the runtime's clock, so a
// headless run is exact.
//
// Semantics (OMUI_EXT_canvas): the canvas states the nodes on screen as its
// items (id `<canvas>/<key>`, the kind's role, a name from its column, the
// node's bounds) and keeps the rest a query away (`find`); the viewport's
// actions move the camera.
export module openmui.ui.canvas:view;

import std;
import openmui.core;
import openmui.view;
import openmui.render;
import openmui.anim;
import :canvas_contract;
import :scene;
import :camera;

export namespace omui {

class Canvas : public CanvasContract<Canvas> {
public:
    static constexpr bool focusable = true;
    static constexpr bool clips = true;

    Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas();

    void mounted();
    void prop_changed(CanvasProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    bool reveal(std::string_view key);
    std::vector<Element> compose();

    ActResult pan_to(ActContext& ctx, const Value& x, const Value& y);
    ActResult zoom_to(ActContext& ctx, const Value& zoom);
    ActResult fit(ActContext& ctx, const Value& target);
    ActResult focus_node(ActContext& ctx, const Value& key);
    ActResult camera_preset(ActContext& ctx, const Value& name);
    ActResult find(ActContext& ctx, const Value& query);

    // ── for the program and canvas-kit ──
    [[nodiscard]] world::Scene* scene() const;
    // The camera now (an animation's current value), and setting it at once.
    [[nodiscard]] world::Camera camera() const;
    void set_camera(const world::Camera& c);
    // Moving it on a spring.
    void animate_to(const world::Camera& c, anim::SpringParams params = anim::springs::standard);
    [[nodiscard]] Size viewport() const;
    // The nodes on screen in the last frame, bottom to top, and their tiers.
    [[nodiscard]] std::span<const world::NodeRef> visible() const noexcept { return visible_; }
    // What the frame drew per tier kind: boxes, custom, promoted (the HUD's).
    struct Stats {
        std::size_t boxes = 0, custom = 0, promoted = 0, visible = 0;
        double      query_ms = 0, paint_ms = 0;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

private:
    void changed();   // after the camera moved: placed, redrawn, told
    void schedule();  // the next frame's animation step
    void step();
    void place();     // the live nodes: composed, or moved with the camera
    [[nodiscard]] std::array<float, 3> placement(const world::Scene& s, world::NodeRef n, Size viewport) const;
    [[nodiscard]] std::size_t tier_of(const world::Kind& k, Rect screen, bool focused) const;

    world::Camera camera_;
    // An animation in flight (a spring to a view, or a fling's glide).
    std::optional<world::CameraMotion> motion_;
    std::optional<world::Inertia>      glide_;
    // Pointers down: one drags, two pinch.
    struct Pointer {
        std::uint32_t id;
        Point         at;
    };
    std::vector<Pointer> pointers_;
    bool                 dragging_ = false;
    Point                velocity_{};      // screen px/s, for the fling
    anim::seconds        last_move_{0};
    float                pinch_distance_ = 0;
    // The frame's view of the scene.
    mutable std::vector<world::NodeRef> visible_;
    std::vector<std::string>             promoted_;   // keys composed as live components
    mutable Stats                        stats_;
    mutable std::uint64_t                drawn_revision_ = 0;
    std::string                          focused_;
    std::uint64_t                        tick_ = 0;   // the pending step's timer
    bool                                 writing_ = false;
};

// Registers the canvas by name, for view documents and immediate mode.
void register_canvas_component();

}  // namespace omui
