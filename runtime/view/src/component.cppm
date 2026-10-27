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
import :model;

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

// Where an overlay goes: against its parent (a menu below its button), or
// centred in the window (a dialog). It flips to stay inside the window.
struct Placement {
    enum class Side : std::uint8_t { below, above, start, end, center };
    enum class Anchor : std::uint8_t { parent, previous };  // previous: the sibling before it (a tooltip's target)
    Side   side = Side::below;
    Anchor anchor = Anchor::parent;
    float offset = 4;           // the gap to the anchor
    bool  modal = false;        // blocks the interface below; Tab stays inside
    bool  scrim = false;        // darkens what is below (theme `scrim`)
    bool  light_dismiss = true; // a press outside performs its `dismiss` action
    bool  match_width = false;  // at least as wide as the anchor (a select's popup)
};

// The runtime's services, as components see them.
class Services {
public:
    virtual ~Services() = default;
    // The tree around a node (components that coordinate: tabs and their tabs).
    [[nodiscard]] virtual NodeId parent(NodeId node) const = 0;
    [[nodiscard]] virtual std::vector<NodeId> children(NodeId node) const = 0;
    [[nodiscard]] virtual Value get(NodeId node, std::string_view property) const = 0;
    // As a script sets it: a two-way binding learns it, a plain property takes it.
    virtual void set(NodeId node, std::string_view property, const Value& value) = 0;
    // Handlers on another node (a tooltip on its target's hover); `off` takes the token.
    virtual std::uint64_t on(NodeId node, std::string event, Handler handler) = 0;
    virtual void off(std::uint64_t token) = 0;
    [[nodiscard]] virtual std::string path(NodeId node) const = 0;
    // The node's default style depends on what changed (a menu that closed).
    virtual void restyle(NodeId node) = 0;
    // A composite component's own children are built again (compose()).
    virtual void recompose(NodeId node) = 0;
    // A model by name, made empty on first use.
    virtual Model& model(std::string_view name) = 0;
    // An outlet: what is mounted under a name while a mounted view shows a
    // slot of that name (a view document's `slot`, a script's slot()). A
    // region that reads it claims the name: Runtime::mount(name, …) then
    // shows there instead of at the top level, before or after.
    [[nodiscard]] virtual Signal<std::optional<Element>> outlet(std::string_view name) = 0;
    // The application's verbs (a view document's `verb` operation).
    [[nodiscard]] virtual Verbs& verbs() noexcept = 0;
    // The node at a path, materialised first if a virtualising ancestor (a
    // list) holds it unmaterialised (spec S-7); none when nothing is there.
    virtual NodeId reveal(std::string_view path) = 0;
    [[nodiscard]] virtual const Theme& theme() const = 0;
    // The person asked for less transparency (a system setting): glass and
    // other see-through materials draw as opaque panels.
    [[nodiscard]] virtual bool reduce_transparency() const { return false; }
    // Physical pixels per logical pixel: what a component rasterising its own
    // textures (a canvas's thumbnails) sizes them by.
    [[nodiscard]] virtual float pixel_scale() const { return 1; }
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
    // Scrolls every scroll view around the node so that it is in view (what
    // the `scroll_into_view` action does): a document's anchors, a list's row.
    virtual void scroll_into_view(NodeId node) = 0;
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
    bool                      overlay = false;  // laid out and painted above everything, placed by `placement`
    std::string_view          cursor;         // the pointer's shape over it: "text", "pointer", "grab" … (empty: inherit)
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
    // An overlay's place (overlays only).
    Placement (*placement)(const void*) = nullptr;
    // A composite component's own children (a select's popup and options),
    // after those its element gave; built again on Services::recompose.
    std::vector<Element> (*compose)(void*) = nullptr;
    // Virtualising components (spec S-7): every item, materialised or not,
    // in order, as the semantic tree states them (`path` is the node's), and
    // materialising one by its key.
    std::vector<spec::SemanticNode> (*virtual_items)(const void*, std::string_view path) = nullptr;
    bool (*reveal)(void*, std::string_view key) = nullptr;
    // A virtualising component whose items need not become nodes to be acted
    // on (a canvas's thousands): one item's semantics by key, stated or not,
    // and its actions, performed by the component.
    std::optional<spec::SemanticNode> (*virtual_item)(const void*, std::string_view path, std::string_view key) = nullptr;
    ActResult (*act_item)(void*, ActContext&, std::string_view key, std::string_view action) = nullptr;
    // After each layout pass, with bounds known (measured sizes, keeping
    // something in view): changes made here show at the next frame.
    void (*laid_out)(void*) = nullptr;
    // A component that shows a model's rows (a list) takes the view of a row
    // from whoever builds it: a builder, or a view document's `for`.
    void (*set_item)(void*, std::function<Element(const Row&, std::size_t)>) = nullptr;
    // A type made at run time (a view document with an interface) carries
    // what its instances are made from: they come from create_in(context).
    const void* context = nullptr;
    void* (*create_in)(const void* context) = nullptr;
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
        if constexpr (requires { C::cursor; }) x.cursor = C::cursor;
        if constexpr (requires { C::overlay; }) x.overlay = C::overlay;
        if constexpr (requires(C& c) { { c.compose() } -> std::same_as<std::vector<Element>>; })
            x.compose = [](void* p) { return static_cast<C*>(p)->compose(); };
        if constexpr (requires(const C& c, std::string_view p) { { c.virtual_items(p) } -> std::same_as<std::vector<spec::SemanticNode>>; })
            x.virtual_items = [](const void* p, std::string_view path) { return static_cast<const C*>(p)->virtual_items(path); };
        if constexpr (requires(const C& c, std::string_view p) { { c.virtual_item(p, p) } -> std::same_as<std::optional<spec::SemanticNode>>; })
            x.virtual_item = [](const void* p, std::string_view path, std::string_view key) { return static_cast<const C*>(p)->virtual_item(path, key); };
        if constexpr (requires(C& c, ActContext& ctx, std::string_view k) { { c.act_item(ctx, k, k) } -> std::same_as<ActResult>; })
            x.act_item = [](void* p, ActContext& ctx, std::string_view key, std::string_view a) { return static_cast<C*>(p)->act_item(ctx, key, a); };
        if constexpr (requires(C& c, std::string_view k) { { c.reveal(k) } -> std::same_as<bool>; })
            x.reveal = [](void* p, std::string_view key) { return static_cast<C*>(p)->reveal(key); };
        if constexpr (requires(C& c) { c.laid_out(); })
            x.laid_out = [](void* p) { static_cast<C*>(p)->laid_out(); };
        if constexpr (requires(const C& c) { { c.placement() } -> std::same_as<Placement>; })
            x.placement = [](const void* p) { return static_cast<const C*>(p)->placement(); };
        if constexpr (requires { C::contract(); }) x.contract = &C::contract();
        if constexpr (requires(C& c, std::function<Element(const Row&, std::size_t)> f) { c.item = std::move(f); })
            x.set_item = [](void* p, std::function<Element(const Row&, std::size_t)> f) { static_cast<C*>(p)->item = std::move(f); };
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
