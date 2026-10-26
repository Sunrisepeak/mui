// openmui.view:impl — the runtime's internals, shared by its implementation
// units (runtime, tree, props, frame, input, act); exported to no one.
module openmui.view:impl;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import :element;
import :theme;
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

struct Visual {
    std::array<float, field_count> f{1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::optional<CornerRadii>     radii;   // per-corner radii (not animated)
    std::string                    shadow;  // a theme shadow token
    bool                           clip = false;
    bool                           visible = true;
    bool                           hit = true;  // pointer_events: none → false

    float&       operator[](Field x) noexcept { return f[static_cast<std::size_t>(x)]; }
    float        operator[](Field x) const noexcept { return f[static_cast<std::size_t>(x)]; }
    [[nodiscard]] Color background() const noexcept {
        return {f[7], f[8], f[9], f[10]};
    }
    [[nodiscard]] Color border() const noexcept { return {f[11], f[12], f[13], f[14]}; }
    [[nodiscard]] CornerRadii corners(Size s) const noexcept {
        return (radii ? *radii : CornerRadii::all((*this)[Field::radius])).fitted(s);
    }
};

struct Node {
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
};

// A change a node asks for, in the order the frame handles them.
enum Dirty : std::uint8_t { dirty_none = 0, dirty_paint = 1, dirty_layout = 2, dirty_semantics = 4, dirty_tree = 8 };

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

    explicit Impl(Runtime& r) : self(r) {}

    Node&       at(NodeId id) { return *nodes.get(id); }
    const Node& at(NodeId id) const { return *nodes.get(id); }
    bool        alive(NodeId id) const { return nodes.contains(id); }
    seconds     now() const { return clock.now(); }
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
    void apply(NodeId id, std::string_view name, const Value& value);
    void set_source(NodeId id, const std::string& name, const Source& source);
    void clear_prop(NodeId id, std::string_view name);
    void sync_style(NodeId id);
    void resolve_visual(NodeId id);
    void animate_field(NodeId id, std::string_view prop, Field field, float target);

    // frames (frame.cpp)
    void   run_timers();
    void   run_animations();
    void   run_layout();
    void   compute_bounds(NodeId id, Point origin, const Affine& parent_world);
    void   paint();
    void   paint_node(NodeId id);
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
