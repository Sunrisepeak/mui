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
import openmui.render.vector;
import hanim;
import :canvas_contract;
import :scene;
import :camera;
import :textures;
import :behavior;

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
    void restyle();
    void laid_out();
    // The page's time moved (D-60): the camera as it was at `t`, from its past.
    void retimed(hanim::seconds t);
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;  // the behaviours' overlays, above the live nodes too
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::vector<spec::SemanticNode> virtual_items(std::string_view path) const;
    std::optional<spec::SemanticNode> virtual_item(std::string_view path, std::string_view key) const;
    ActResult act_item(ActContext& ctx, std::string_view key, std::string_view action);
    bool reveal(std::string_view key);
    std::vector<Element> compose();

    ActResult pan_to(ActContext& ctx, const PanToArgs& args);
    ActResult zoom_to(ActContext& ctx, const Value& zoom);
    ActResult fit(ActContext& ctx, const Value& target);
    ActResult focus_node(ActContext& ctx, const Value& key);
    ActResult move_by(ActContext& ctx, const MoveByArgs& args);
    ActResult camera_preset(ActContext& ctx, const Value& name);
    ActResult find(ActContext& ctx, const Value& query);

    // ── for the program and canvas-kit ──
    [[nodiscard]] world::Scene* scene() const;
    // The camera now (an animation's current value), and setting it at once.
    [[nodiscard]] world::Camera camera() const;
    void set_camera(const world::Camera& c);
    // Moving it on a spring.
    void animate_to(const world::Camera& c, hanim::SpringParams params = hanim::springs::standard);
    [[nodiscard]] Size viewport() const;
    // The nodes on screen in the last frame, bottom to top, and their tiers.
    [[nodiscard]] std::span<const world::NodeRef> visible() const noexcept { return visible_; }
    // What the frame drew per tier kind: boxes, custom, promoted (the HUD's).
    struct Stats {
        std::size_t boxes = 0, custom = 0, textured = 0, stretched = 0, promoted = 0, visible = 0;
        double      query_ms = 0, paint_ms = 0;
    };
    [[nodiscard]] world::Textures::Stats texture_stats() const { return textures_.stats(); }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    // The selection (keys, in the order chosen), and choosing it.
    [[nodiscard]] const std::vector<std::string>& selection() const noexcept { return selected_; }
    void select(std::vector<std::string> keys);
    // The behaviours this canvas holds (its `behaviors`).
    [[nodiscard]] world::Behavior* behavior(std::string_view name) const;

private:
    void changed(bool record = true);   // after the camera moved: placed, redrawn, told (and remembered)
    // The camera's past (D-60): from when it rested where, or moved how — the
    // last 30 s, so a page whose time is taken back shows it as it was.
    struct Course {
        hanim::seconds                     from{0};
        world::Camera                      at;
        std::optional<world::CameraMotion> motion;
        std::optional<world::Inertia>      glide;
    };
    std::vector<Course> past_;
    void remember();
    [[nodiscard]] const Course* course_at(hanim::seconds t) const;
    void schedule();  // the next frame's animation step
    void step();
    void pace();      // the frame demand while the camera moves on its own
    void place();     // the live nodes: composed, or moved with the camera
    [[nodiscard]] std::array<float, 3> placement(const world::Scene& s, world::NodeRef n, Size viewport) const;
    [[nodiscard]] std::size_t tier_of(const world::Kind& k, Rect screen, bool focused) const;
    void build_behaviors();
    [[nodiscard]] world::PointerInput pointer_input(const PointerEvent& p, Point at) const;
    spec::SemanticNode item(const world::Scene& s, world::NodeRef n, std::string_view path, Size vp, Rect bounds) const;
    void paint_edges(const world::Scene& s, render::DisplayList& list, Size vp) const;

    // What behaviours see of the canvas.
    struct HostAdapter final : world::Host {
        Canvas* c = nullptr;
        world::Scene& scene() override;
        world::Camera camera() const override { return c->camera_; }
        Size viewport() const override { return c->viewport(); }
        void set_camera(const world::Camera& cam) override { c->set_camera(cam); }
        void animate_to(const world::Camera& cam) override { c->animate_to(cam); }
        Services& services() override { return c->services(); }
        void redraw() override { c->services().invalidate_paint(c->node()); }
        const std::vector<std::string>& selection() const override { return c->selected_; }
        void select(std::vector<std::string> keys) override { c->select(std::move(keys)); }
        void committed(std::string_view what, const Value& args) override;
        world::Behavior* behavior(std::string_view name) override { return c->behavior(name); }
    };
    mutable HostAdapter                           host_;
    std::vector<std::unique_ptr<world::Behavior>> behaviors_;
    world::Behavior*                              gesture_ = nullptr;  // the behaviour that took the pointer
    std::vector<std::string>                      selected_;

    world::Camera camera_;
    world::Camera written_{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};  // the camera last told to the bindings
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
    Point                pressed_at_{};    // where the press no behaviour took began (a tap ends there)
    bool                 travelled_ = false;
    hanim::seconds        last_move_{0};
    float                pinch_distance_ = 0;
    // The frame's view of the scene.
    mutable std::vector<world::NodeRef> visible_;
    std::vector<std::string>             promoted_;   // keys composed as live components
    std::vector<std::uint64_t>           promoted_revisions_;  // their content revisions when composed (Scene::touch)
    mutable Stats                        stats_;
    mutable std::uint64_t                drawn_revision_ = 0;
    std::string                          focused_;
    std::uint64_t                        tick_ = 0;   // the pending step's timer
    // Nodes go live once the camera has rested this long (seconds).
    static constexpr double              promote_after = 0.12;
    hanim::seconds                        last_motion_{-1e9};
    std::uint64_t                        promote_timer_ = 0;
    FrameDemandLease                     pace_;
    bool                                 writing_ = false;
    bool                                 started_ = false;  // `start` has placed the camera
    Size                                 placed_for_{};     // the viewport the live nodes were placed for
    bool                                 refit_ = false;    // fitted by `start`, untouched since: fits again when its size changes
    Canvas*                              self_ = nullptr;  // for the steps the painting (const) schedules
    // The scene observed: its changes are drawn (coalesced to one repaint)
    // and its live nodes placed again at the next step.
    void                                 observe();
    std::string                          observed_;
    world::Scene*                        watched_ = nullptr;  // the scene `observer_` is on (a scene dropped and made again is another)
    std::uint64_t                        observer_ = 0;
    bool                                 resync_ = false;     // changed since the step last placed the live nodes
    mutable world::Textures              textures_;
    mutable std::uint64_t                frame_ = 0;
};

// Registers the canvas by name, for view documents and immediate mode.
void register_canvas_component();

}  // namespace omui
