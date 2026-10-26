// openmui.view:component — what a component is to the runtime.
//
// A component is a C++ class; `component_type<C>()` derives its function
// table at compile time from what the class defines (paint, measure,
// on_event, act, semantics, set_prop …), so a component writes only what it
// needs and pays no virtual call for what it lacks. The table is the one
// coarse boundary (one indirect call per node per phase, never per pixel or
// glyph); contract-generated bases (omui-gen) produce the same tables, which
// is why official and third-party components are alike.
export module openmui.view:component;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import :element;
import :theme;

export namespace omui {

struct NodeTag;
using NodeId = Handle<NodeTag>;

// What the runtime knows about a node's interaction state.
struct NodeState {
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool focus_visible = false;  // focused by keyboard: draw the ring
    bool disabled = false;
    bool removing = false;
    bool animating = false;
};

// The runtime's services, as components see them.
class Services {
public:
    virtual ~Services() = default;
    [[nodiscard]] virtual const Theme& theme() const = 0;
    [[nodiscard]] virtual typeset::FontCollection& fonts() = 0;
    [[nodiscard]] virtual render::ImageStore& images() = 0;
    [[nodiscard]] virtual anim::seconds now() const = 0;
    [[nodiscard]] virtual Rect bounds(NodeId node) const = 0;        // window coordinates
    // The extent of the node's children (a scroll view's scroll range).
    [[nodiscard]] virtual Size content_size(NodeId node) const = 0;
    // The node's padding and border: its content box is its bounds inset by these.
    [[nodiscard]] virtual Insets content_insets(NodeId node) const = 0;
    [[nodiscard]] virtual NodeState state(NodeId node) const = 0;
    virtual void emit(NodeId node, std::string_view event, const Value& args) = 0;
    // A bindable property changed from inside (a slider moved): its source
    // learns the value (two-way binding).
    virtual void write_back(NodeId node, std::string_view property, const Value& value) = 0;
    virtual void invalidate_paint(NodeId node) = 0;
    virtual void invalidate_layout(NodeId node) = 0;
    virtual void invalidate_semantics(NodeId node) = 0;
    virtual void request_frame() = 0;
    virtual void focus(NodeId node) = 0;
    virtual void capture_pointer(NodeId node) = 0;
    virtual void release_pointer() = 0;
    // Text input (input methods): where the caret is, so the candidate
    // window follows it.
    virtual void text_input(NodeId node, std::optional<Rect> caret) = 0;
    [[nodiscard]] virtual std::string clipboard_text() = 0;
    virtual void set_clipboard_text(std::string_view text) = 0;
    // A spring or tween on one scalar of the node (scroll offsets, thumbs).
    virtual anim::Set& animations() = 0;
    // Runs `fn` at the first frame at or after now + delay, on the clock of
    // the runtime (virtual and render clocks make timers deterministic).
    virtual std::uint64_t after(seconds delay, std::function<void()> fn) = 0;
    virtual void cancel(std::uint64_t timer) = 0;
};

struct PaintContext {
    Services&            services;
    render::DisplayList& list;
    NodeId               node;
    Size                 size;     // the node's border box; drawing is in its local coordinates
    Rect                 content;  // the content box (inside padding and border), local coordinates
    NodeState            state;
    const Theme&         theme;
};

struct MeasureContext {
    Services& services;
    NodeId    node;
};

struct EventContext {
    Services& services;
    NodeId    node;
    Point     local;          // pointer position in the node's coordinates
    bool      handled = false;
    bool      stopped = false;

    void stop() noexcept { stopped = handled = true; }
    void emit(std::string_view event, const Value& args = {}) { services.emit(node, event, args); }
    void repaint() { services.invalidate_paint(node); }
    void relayout() { services.invalidate_layout(node); }
};

struct ActResult {
    bool        ok = true;
    Value       data;
    std::string error;  // refused or failed: why

    [[nodiscard]] static ActResult done(Value v = {}) { return {true, std::move(v), {}}; }
    [[nodiscard]] static ActResult fail(std::string why) { return {false, {}, std::move(why)}; }
};

struct ActContext {
    Services&    services;
    NodeId       node;
    const Value& args;
};

struct SemanticsContext {
    Services& services;
    NodeId    node;
    NodeState state;
};

// The function table of a component type.
struct ComponentType {
    std::string_view          name;
    const spec::ContractInfo* contract = nullptr;
    Role                      role = Role::generic;
    bool                      focusable = false;
    bool                      clips = false;  // children are clipped to the node
    void* (*create)() = nullptr;
    void (*destroy)(void*) = nullptr;
    void (*mount)(void*, Services&, NodeId) = nullptr;
    bool (*set_prop)(void*, std::string_view, const Value&) = nullptr;  // false: not this component's
    Value (*get_prop)(const void*, std::string_view) = nullptr;
    void (*style)(const void*, const Theme&, layout::Style&) = nullptr;
    Size (*measure)(void*, MeasureContext&, std::optional<float>, std::optional<float>, layout::Available, layout::Available) = nullptr;
    float (*baseline)(void*, MeasureContext&, Size) = nullptr;
    void (*paint)(const void*, PaintContext&) = nullptr;
    void (*paint_over)(const void*, PaintContext&) = nullptr;  // after the children (focus rings, scroll bars)
    bool (*event)(void*, EventContext&, const InputEvent&) = nullptr;
    ActResult (*act)(void*, ActContext&, std::string_view) = nullptr;
    void (*semantics)(const void*, SemanticsContext&, spec::SemanticNode&) = nullptr;
    // A scroll container: its children are offset by this (and clipped).
    Point (*scroll_offset)(const void*) = nullptr;
    // The theme changed: drop what was resolved from it (shaped text).
    void (*restyle)(void*) = nullptr;
};

// The function table of component class C. C declares `static constexpr
// std::string_view name`, and optionally `role`, `focusable`, `clips`,
// `contract`, and member functions with the shapes the table names.
template <class C>
[[nodiscard]] const ComponentType& component_type() {
    static const ComponentType t = [] {
        ComponentType x;
        x.name = C::name;
        if constexpr (requires { C::role; }) x.role = C::role;
        if constexpr (requires { C::focusable; }) x.focusable = C::focusable;
        if constexpr (requires { C::clips; }) x.clips = C::clips;
        if constexpr (requires { C::contract(); }) x.contract = &C::contract();
        x.create = [] { return static_cast<void*>(new C()); };
        x.destroy = [](void* p) { delete static_cast<C*>(p); };
        if constexpr (requires(C& c, Services& s, NodeId n) { c.mount(s, n); })
            x.mount = [](void* p, Services& s, NodeId n) { static_cast<C*>(p)->mount(s, n); };
        if constexpr (requires(C& c, const Value& v) { { c.set_prop(std::string_view{}, v) } -> std::same_as<bool>; })
            x.set_prop = [](void* p, std::string_view k, const Value& v) { return static_cast<C*>(p)->set_prop(k, v); };
        if constexpr (requires(const C& c) { { c.get_prop(std::string_view{}) } -> std::same_as<Value>; })
            x.get_prop = [](const void* p, std::string_view k) { return static_cast<const C*>(p)->get_prop(k); };
        if constexpr (requires(const C& c, const Theme& t, layout::Style& s) { c.style(t, s); })
            x.style = [](const void* p, const Theme& t, layout::Style& s) { static_cast<const C*>(p)->style(t, s); };
        if constexpr (requires(C& c, MeasureContext& m, std::optional<float> w, layout::Available a) {
                          { c.measure(m, w, w, a, a) } -> std::same_as<Size>;
                      })
            x.measure = [](void* p, MeasureContext& m, std::optional<float> w, std::optional<float> h, layout::Available aw,
                           layout::Available ah) { return static_cast<C*>(p)->measure(m, w, h, aw, ah); };
        if constexpr (requires(C& c, MeasureContext& m, Size s) { { c.baseline(m, s) } -> std::same_as<float>; })
            x.baseline = [](void* p, MeasureContext& m, Size s) { return static_cast<C*>(p)->baseline(m, s); };
        if constexpr (requires(const C& c, PaintContext& ctx) { c.paint(ctx); })
            x.paint = [](const void* p, PaintContext& ctx) { static_cast<const C*>(p)->paint(ctx); };
        if constexpr (requires(const C& c, PaintContext& ctx) { c.paint_over(ctx); })
            x.paint_over = [](const void* p, PaintContext& ctx) { static_cast<const C*>(p)->paint_over(ctx); };
        if constexpr (requires(C& c, EventContext& ctx, const InputEvent& e) { { c.on_event(ctx, e) } -> std::same_as<bool>; })
            x.event = [](void* p, EventContext& ctx, const InputEvent& e) { return static_cast<C*>(p)->on_event(ctx, e); };
        if constexpr (requires(C& c, ActContext& ctx) { { c.act(ctx, std::string_view{}) } -> std::same_as<ActResult>; })
            x.act = [](void* p, ActContext& ctx, std::string_view a) { return static_cast<C*>(p)->act(ctx, a); };
        if constexpr (requires(const C& c, SemanticsContext& ctx, spec::SemanticNode& n) { c.semantics(ctx, n); })
            x.semantics = [](const void* p, SemanticsContext& ctx, spec::SemanticNode& n) { static_cast<const C*>(p)->semantics(ctx, n); };
        if constexpr (requires(const C& c) { { c.scroll_offset() } -> std::same_as<Point>; })
            x.scroll_offset = [](const void* p) { return static_cast<const C*>(p)->scroll_offset(); };
        if constexpr (requires(C& c) { c.restyle(); })
            x.restyle = [](void* p) { static_cast<C*>(p)->restyle(); };
        return x;
    }();
    return t;
}

}  // namespace omui
