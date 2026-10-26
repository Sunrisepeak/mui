// openmui.view:impl — the runtime's internals, shared by its implementation
// units (runtime, tree, props, frame, input, act); exported to no one.
module openmui.view:impl;

import std;
import openmui.core;
import openmui.layout;
import openmui.anim;
import openmui.pipeline;
import openmui.render;
import openmui.text;
import :element;
import :theme;
import :model;
import :component;
import :runtime;

namespace omui {

struct RegionState;

// One child position of a node: a node, or a region of positions.
struct ChildSlot {
    NodeId                       node;
    std::shared_ptr<RegionState> region;
};

// Children rebuilt when what their builder reads changes. The builder runs
// inside a watch; its elements wait in `pending` until the frame boundary,
// where they are reconciled with the nodes by key.
struct RegionState {
    NodeId                  parent;
    std::shared_ptr<Region> region;
    std::vector<ChildSlot>  slots;
    std::vector<Element>    pending;
    bool                    dirty = false;
    bool                    building = true;  // the watch's first run: elements are taken at once
    Watch                   watch;
};

// The scalar parts of a node's look, by index: the animation channels of
// transitions drive them directly.
enum class Field : std::uint8_t {
    opacity, translate_x, translate_y, scale, rotation, radius, border_width,
    background_r, background_g, background_b, background_a,
    border_r, border_g, border_b, border_a,
    count_,
};
inline constexpr std::size_t field_count = static_cast<std::size_t>(Field::count_);
inline constexpr std::uint32_t background_bits = 0xfu << static_cast<unsigned>(Field::background_r);
inline constexpr std::uint32_t border_bits = 0xfu << static_cast<unsigned>(Field::border_r);

// A colour as the lanes it animates in (OKLab premultiplied by alpha, alpha), and back.
[[nodiscard]] inline std::array<float, 4> color_lanes(const Color& c) noexcept {
    const auto l = anim::lanes<anim::Rgba>::to(anim::Rgba{c.r, c.g, c.b, c.a});
    return {static_cast<float>(l[0]), static_cast<float>(l[1]), static_cast<float>(l[2]), static_cast<float>(l[3])};
}
[[nodiscard]] inline Color lanes_color(const float* f) noexcept {
    const anim::Rgba c = anim::lanes<anim::Rgba>::from({f[0], f[1], f[2], f[3]});
    return {c.r, c.g, c.b, c.a};
}

struct Visual {
    std::array<float, field_count> f{1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::optional<CornerRadii>     radii;   // per-corner radii (not animated)
    std::string                    shadow;  // a theme shadow token
    bool                           clip = false;
    bool                           visible = true;
    bool                           hit = true;  // pointer_events: none → false

    float&       operator[](Field x) noexcept { return f[static_cast<std::size_t>(x)]; }
    float        operator[](Field x) const noexcept { return f[static_cast<std::size_t>(x)]; }
    // The colours' channels in `f` are anim's lanes (OKLab premultiplied by
    // alpha, then alpha), so a transition travels through OKLab (D-60). What
    // is painted is the exact colour that was set while nothing moves, and the
    // lanes' colour in between.
    Color background_color = Color::transparent(), background_target = Color::transparent();
    Color border_color = Color::transparent(), border_target = Color::transparent();
    [[nodiscard]] Color background() const noexcept { return background_color; }
    [[nodiscard]] Color border() const noexcept { return border_color; }
    [[nodiscard]] CornerRadii corners(Size s) const noexcept {
        return (radii ? *radii : CornerRadii::all((*this)[Field::radius])).fitted(s);
    }
};

struct Node {
    // Move-only, and said so: its watches cannot be copied. Without this a
    // standard library whose std::map move may throw (MSVC's) makes
    // vector<optional<Node>> copy on growth and instantiates a copy of them.
    Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&) = default;
    Node& operator=(Node&&) = default;
    ~Node() = default;

    const ComponentType* type = nullptr;
    void*                instance = nullptr;
    std::string          segment;       // explicit id or structural (S-8)
    bool                 explicit_id = false;
    std::string          key;
    NodeId               parent;
    std::vector<ChildSlot> slots;
    std::vector<NodeId>  children;      // the slots flattened
    layout::NodeId       layout = layout::no_node;

    std::map<std::string, Value, std::less<>>      props;     // the current value of every set property
    std::vector<std::string>                       element_props;  // the names its element set
    std::map<std::string, Source, std::less<>>     sources;   // where bound properties come from
    std::map<std::string, Watch, std::less<>>      bindings;
    std::vector<std::pair<std::string, Handler>>   handlers;
    std::map<std::string, Transition, std::less<>> transitions;

    Visual               visual;
    std::array<anim::Set::Channel, field_count> channels{};
    std::uint32_t        moving = 0;    // bit per field: its channel is still moving
    // Any other property in transition (D-60): layout properties, a
    // component's numbers and colours. Each frame applies its value.
    struct PropAnimation {
        enum class Kind : std::uint8_t { number, integer, percent, color, list };
        std::string                       name;
        Kind                              kind = Kind::number;
        std::uint8_t                      lanes = 1;
        std::array<anim::Set::Channel, 4> channels{};
        Value                             target;
        bool                              resting = false;  // settled: kept, so seeking back in time replays it
    };
    std::vector<PropAnimation> prop_animations;
    [[nodiscard]] bool props_moving() const noexcept {
        return std::ranges::any_of(prop_animations, [](const PropAnimation& a) { return !a.resting; });
    }
    NodeState            state;
    std::optional<bool>  focusable;
    bool                 shown = false; // painted once: later changes may animate

    Rect                 frame;         // relative to the parent's border box (layout)
    Rect                 bounds;        // window coordinates, without visual transforms
    Point                content_offset;
    Affine               local;         // parent → node: its frame and visual transforms
    Affine               world;         // node → window
    std::source_location where;
    std::uint32_t        index = 0;     // display-list node marker; 0 is none
    std::shared_ptr<RegionState> composed;  // a composite component's own children
};

// A change a node asks for, in the order the frame handles them.
enum Dirty : std::uint8_t { dirty_none = 0, dirty_paint = 1, dirty_layout = 2, dirty_semantics = 4, dirty_tree = 8 };

// The last frames' statistics: a fixed ring, so recording a frame allocates
// nothing (and tools read it oldest first).
struct FrameRing {
    static constexpr std::size_t capacity = 240;
    std::array<FrameStats, capacity> items{};
    std::size_t                      head = 0;   // the oldest
    std::size_t                      count = 0;

    void push(const FrameStats& f) noexcept {
        items[(head + count) % capacity] = f;
        if (count < capacity) ++count;
        else head = (head + 1) % capacity;
    }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] const FrameStats& operator[](std::size_t i) const noexcept { return items[(head + i) % capacity]; }
    [[nodiscard]] const FrameStats& back() const noexcept { return (*this)[count - 1]; }
};

struct Runtime::Impl {
    Runtime&                              self;
    RuntimeOptions                        options;
    Theme                                 theme;
    std::unique_ptr<typeset::FontCollection> own_fonts;
    typeset::FontCollection*                 fonts = nullptr;
    render::ImageStore                    images;
    anim::Clock                           clock;
    anim::Set                             anims;
    layout::Engine                        layout;
    Verbs                                 verbs;
    UiQueue                               queue;
    SlotMap<Node, NodeTag>                nodes;
    std::vector<NodeId>                   by_index;   // node index → handle (0 unused)
    std::vector<std::uint32_t>            free_indices;
    std::vector<NodeId>                   by_layout;  // layout node → node
    NodeId                                root;
    std::vector<std::pair<std::string, NodeId>> mounted;  // slot → node
    render::DisplayList                   list;
    std::unordered_map<std::string, NodeId> path_index;
    bool                                  paths_dirty = true;

    std::uint8_t                          dirty = dirty_paint | dirty_layout | dirty_semantics | dirty_tree;
    bool                                  frame_requested = true;
    std::vector<std::weak_ptr<RegionState>> dirty_regions;
    std::vector<NodeId>                   animating;  // nodes with moving channels
    std::vector<NodeId>                   removing;   // nodes playing their exit transition (S-4)
    std::vector<NodeId>                   fresh;      // created since the last frame ended
    std::vector<NodeId>                   overlays;   // overlay nodes, bottom to top
    std::vector<NodeId>                   watchers;   // nodes whose type has laid_out()
    std::vector<std::pair<NodeId, NodeId>> modal_focus;
    std::map<std::string, std::unique_ptr<Model>, std::less<>> models;  // a modal overlay, and the focus before it opened
    Size                                  size;
    float                                 scale = 1;
    bool                                  reduce_motion = false;

    // input
    NodeId               capture;
    NodeId               pressed;
    std::vector<NodeId>  hover_chain;   // the hovered node and its ancestors
    NodeId               focus;
    bool                 focus_visible = false;
    Point                pointer;
    std::optional<Rect>  text_input;
    NodeId               text_input_node;
    std::string          clipboard;
    std::function<std::string()>            clipboard_read;
    std::function<void(std::string_view)>   clipboard_write;

    std::uint64_t        semantic_revision = 1;
    std::uint64_t        next_token = 1;
    struct RuntimeHandler {
        std::uint64_t token;
        NodeId        node;
        std::string   event;
        Handler       fn;
    };
    std::vector<RuntimeHandler> runtime_handlers;
    struct Timer {
        seconds               due;
        std::uint64_t         token;
        std::function<void()> fn;
    };
    std::vector<Timer>          timers;
    std::vector<ActionEntry>    journal;
    FrameStats                  stats;
    FrameRing                   history;
    // The frame as a pipeline of stage plugins (stages.cpp, D-66).
    pipeline::Registry                 registry;
    std::optional<pipeline::Pipeline>  frames;
    pipeline::Resources                resources;
    FrameTree                          tree_token;
    FrameLayout                        layout_token;
    FrameBounds                        bounds_token;
    std::uint8_t                       todo = 0;           // what the bounds stage found dirty
    bool                               dirty_taken = false;  // a bounds stage took `dirty` this frame
    bool                               bounds_ran = false;
    bool                               repainted = false;
    bool                               placed = true;      // the pipeline computes bounds (H1 and up)
    void register_stages();
    void build_pipeline();
    std::vector<std::pair<NodeId, seconds>> flashes;  // debug: nodes that changed, and when
    seconds                     last_slow_log{-1e9};  // slow frames are logged at most once a second

    explicit Impl(Runtime& r) : self(r) {}

    Node&       at(NodeId id) { return *nodes.get(id); }
    const Node& at(NodeId id) const { return *nodes.get(id); }
    bool        alive(NodeId id) const { return nodes.contains(id); }
    // The time the application sees: the host's, or the host's mapped by
    // the time controls (paused, slowed, stepped, sought) once a tool uses them.
    struct TimeControl {
        bool                   controlled = false;
        bool                   paused = false;
        double                 rate = 1;
        seconds                host_last{0};
        seconds                shown{0};
        seconds                step{0};
        std::optional<seconds> seek;
    };
    TimeControl time_control;
    seconds     now() const { return time_control.controlled ? time_control.shown : clock.now(); }
    void        take_time_control();
    void        apply_time_control();
    void        retime();  // every live channel re-evaluated at now()
    void        touch(std::uint8_t what) {
        dirty |= what;
        frame_requested = true;
        if (what & (dirty_semantics | dirty_tree)) ++semantic_revision;
    }

    // tree (tree.cpp)
    NodeId create(Element& e, NodeId parent);
    void   destroy(NodeId id);
    std::vector<ChildSlot> reconcile(NodeId parent, std::vector<ChildSlot> old, std::vector<Element>& elements);
    void   run_region(const std::shared_ptr<RegionState>& region);
    void   destroy_slot(ChildSlot& slot);
    void   enter(NodeId id);
    void   reconcile_region(RegionState& region);
    void   reconcile_regions();
    void   update_from(NodeId id, Element& e);
    void   flatten(NodeId id);
    void   assign_segments(NodeId parent);
    void   rebuild_paths();
    void   start_removal(NodeId id);
    void   finish_removals();

    // properties (props.cpp)
    // Sets a property; when a transition covers it and both ends interpolate,
    // starts (or retargets) the transition instead, and frames apply it.
    void apply(NodeId id, std::string_view name, const Value& value);
    void apply_now(NodeId id, std::string_view name, const Value& value);
    bool animate_prop(NodeId id, std::string_view name, const Value& value);
    void step_props(NodeId id, seconds t, const anim::Policy& policy);
    void stop_prop(NodeId id, std::string_view name);
    [[nodiscard]] const Transition* transition_for(const Node& n, std::string_view prop) const;
    void set_source(NodeId id, const std::string& name, const Source& source);
    void clear_prop(NodeId id, std::string_view name);
    void sync_style(NodeId id);
    void resolve_visual(NodeId id);
    void animate_field(NodeId id, std::string_view prop, Field field, float target);

    // frames (frame.cpp)
    void   run_timers();
    void   run_animations();
    void   run_layout();
    void   compute_bounds(NodeId id, Point origin, const Affine& parent_world, std::optional<Point> placed = std::nullopt);
    void   place_overlays();
    void   trap_focus();
    [[nodiscard]] bool displayed(NodeId id) const;
    [[nodiscard]] NodeId topmost_modal() const;
    [[nodiscard]] bool contains(NodeId ancestor, NodeId id) const;
    void   paint();
    void   paint_node(NodeId id);
    void   paint_debug();
    NodeId hit(NodeId id, Point window, std::optional<Rect> clip) const;

    // input (input.cpp)
    void   dispatch(const InputEvent& e);
    void   pointer_event(const PointerEvent& e);
    void   wheel_event(const WheelEvent& e);
    void   key_event(const KeyEvent& e);
    bool   deliver(NodeId target, const InputEvent& e, Point window, bool bubble);
    void   set_hover(NodeId target);
    void   set_focus(NodeId id, bool keyboard);
    [[nodiscard]] bool is_focusable(NodeId id) const;
    [[nodiscard]] std::vector<NodeId> focus_order() const;
    [[nodiscard]] NodeId focusable_ancestor(NodeId id) const;
    [[nodiscard]] NodeId activatable_ancestor(NodeId id) const;
    bool   key_binding(NodeId id, const KeyEvent& e);

    // actions and semantics (act.cpp)
    void call_handlers(NodeId id, std::string_view event, const Value& args);
    [[nodiscard]] bool has_handler(NodeId id, std::string_view event) const;
    Envelope perform(NodeId id, std::string_view action, const Invocation& call, bool command);
    [[nodiscard]] spec::SemanticNode semantics(NodeId id) const;
};

// Pointer position in a node's local coordinates.
[[nodiscard]] inline Point local_point(const Node& n, Point window) {
    if (auto inv = n.world.inverse()) return inv->apply(window);
    return {window.x - n.bounds.x, window.y - n.bounds.y};
}

// "set_value(max)" → {"set_value", ["max"]}; "next" → {"next", []}.
struct KeyTarget {
    std::string              name;
    std::vector<std::string> args;
};
[[nodiscard]] KeyTarget parse_key_target(std::string_view text);

// The property names the runtime itself interprets.
[[nodiscard]] bool is_layout_prop(std::string_view name) noexcept;
[[nodiscard]] bool is_visual_prop(std::string_view name) noexcept;

}  // namespace omui
