// openmui.view:runtime — the tree of a running interface and its frames.
//
// The runtime owns the nodes (a component instance each, with properties,
// bindings, handlers, transitions and a layout box), runs a frame
// (§3.1 of the plan) —
//
//   input → handlers and actions → signals → regions rebuilt → animations
//   → layout (incremental) → paint (display list)
//
// — and answers the questions the semantic layer, agents, scripts and tests
// ask: what is at this path, what does this node say about itself, perform
// this action. Everything runs on the thread that owns the runtime; other
// threads post work through the queue.
export module openmui.view:runtime;

import std;
import openmui.core;
import openmui.layout;
import openmui.render.vector;
import openmui.pipeline;
import openmui.text;
import :element;
import :theme;
import :model;
import :component;

export namespace omui {

// Development aids drawn over the interface (never in what tests pin).
struct DebugOptions {
    bool   bounds = false;     // every node's border box, coloured by depth
    bool   repaints = false;   // nodes whose properties changed flash
    bool   inspector = false;  // the hovered node: its boxes, path, type, size and source line
    bool   hud = false;        // frame time (mean, p95), nodes, layout and paint counts
    double slow_frame_ms = 0;  // a frame slower than this logs its phases ("frame" category); 0: off
};

// How far a frame goes: the semantic tree alone (H0: build and reconcile),
// with layout (H1: bounds, hit testing), or to a display list (H2 and up).
enum class Stages : std::uint8_t { semantics, layout, paint };

struct RuntimeOptions {
    Theme                 theme = Theme::light();
    typeset::FontCollection* fonts = nullptr;  // nullptr: the runtime loads the default fonts
    hanim::Clock           clock = hanim::Clock::virtual_time();
    Size                  size{800, 600};   // logical pixels
    float                 scale = 1.0f;     // physical pixels per logical pixel
    bool                  reduce_motion = false;
    bool                  reduce_transparency = false;  // see-through materials (glass) drawn opaque
    Stages                stages = Stages::paint;
    DebugOptions          debug;
    // The frame's pipeline (D-66): nullopt is runtime_pipeline(stages). A
    // custom one may replace, add or leave out stages; `plugins` adds the
    // stage plugins it names to the runtime's registry.
    std::optional<pipeline::Description>            pipeline;
    std::function<void(pipeline::Registry&)>        plugins;
};

// What the stages of a frame exchange (pipeline resources). The tree, the
// layout and the bounds are the runtime's own state, passed as tokens: a
// stage reading `bounds` runs after the stage that computes them.
struct FrameTree {
    class Runtime* runtime = nullptr;
};
struct FrameLayout {
    class Runtime* runtime = nullptr;
};
struct FrameBounds {
    class Runtime* runtime = nullptr;
};
namespace frame_resource {
inline constexpr std::string_view input = "frame";     // FrameInput, from the caller
inline constexpr std::string_view tree = "tree";       // FrameTree, from the caller
inline constexpr std::string_view layout = "layout";   // FrameLayout
inline constexpr std::string_view bounds = "bounds";   // FrameBounds
inline constexpr std::string_view display = "display"; // render::DisplayList
}  // namespace frame_resource

// A style's effect on what a selector names (D-69): "window" (the whole
// window), a component type ("button"), or "role:<role>" ("role:switch").
// `effect` is written as the pixel_effect property is: {name, params…}. A
// node's own pixel_effect wins.
struct EffectRule {
    std::string selector;
    Value       effect;
};

struct FrameInput {
    std::optional<hanim::seconds> time;   // virtual and render clocks: the frame's time
    std::vector<InputEvent>      events;
    // Interactive clock: when the host predicts this frame reaches the
    // screen; its animations are evaluated there, so motion keeps pace with
    // what is seen whatever the refresh rate (D-70). Unset: now.
    std::optional<std::chrono::steady_clock::time_point> present_at;
    // The refresh rate of the display the frame goes to (0: unknown).
    double display_hz = 0;
};

}  // namespace omui

export namespace omui::pipeline {
template <>
struct resource_type<omui::FrameInput> {
    static constexpr std::string_view name = "omui.FrameInput";
};
template <>
struct resource_type<omui::FrameTree> {
    static constexpr std::string_view name = "omui.FrameTree";
};
template <>
struct resource_type<omui::FrameLayout> {
    static constexpr std::string_view name = "omui.FrameLayout";
};
template <>
struct resource_type<omui::FrameBounds> {
    static constexpr std::string_view name = "omui.FrameBounds";
};
template <>
struct resource_type<omui::render::DisplayList> {
    static constexpr std::string_view name = "render.DisplayList";
};
}  // namespace omui::pipeline

export namespace omui {

// Where a frame's time went, in milliseconds (also traced as spans of the
// category "frame" while openmui.trace records).
struct FrameTimings {
    double timers = 0, input = 0, regions = 0, animations = 0, layout = 0, bounds = 0, paint = 0;
    double other = 0;  // stages a custom pipeline added
    double total = 0;
};

struct FrameStats {
    std::size_t  nodes = 0;
    std::size_t  laid_out = 0;    // layout nodes computed (not from cache)
    std::size_t  painted = 0;     // nodes painted (0 when the list was reused)
    std::size_t  bindings = 0;    // property bindings re-evaluated
    std::size_t  animating = 0;   // animation channels still moving
    FrameTimings timings;
};


struct FrameOutput {
    const render::DisplayList* list = nullptr;  // the runtime's list, valid until the next frame
    bool                       repainted = false;
    bool                       animating = false;  // another frame is wanted
    std::uint64_t              revision = 0;       // semantic revision after the frame
    FrameStats                 stats;
    std::optional<seconds>     next_timer;         // when the earliest timer is due (hosts sleep until then)
    FrameDemand                demand;             // how often frames are wanted from now on, and why (D-70)
};

// One performed action, for audit (openas: who did what, with what result).
struct ActionEntry {
    std::string path;
    std::string action;
    std::string actor;
    int         exit_code = 0;
};

// What a history holds (S-12), for tools and agents: oldest first.
struct HistoryItem {
    std::string label;          // "toggle on 'main/dark'", or a transaction's own
    std::size_t steps = 1;      // the actions in it (a transaction's several)
    bool        barrier = false;  // irreversible or external: nothing before it can be undone
};
struct History {
    std::string              scope;  // the scope's path ("app": the window)
    std::vector<HistoryItem> undo, redo;
};

// How many steps a scope's history keeps; the oldest go first.
inline constexpr std::size_t history_limit = 200;

// The process's task pool (Tasks::shared) as the CPU renderer's Parallel:
// hosts draw bands of rows at the same time, with the bytes of a serial render.
[[nodiscard]] render::Parallel& raster_parallel() noexcept;

// The runtime's frame pipeline for a level: H0 (semantics) is timers, input,
// regions and animations; H1 (layout) adds layout and bounds; H2 (paint)
// adds paint and the debug overlay. Its stage plugins are
// `openmui.view.<stage>`.
[[nodiscard]] pipeline::Description runtime_pipeline(Stages stages);

class Runtime final : public Services {
public:
    explicit Runtime(RuntimeOptions options = {});
    ~Runtime() override;
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // ── mounting ──
    // Mounts a view at a slot (a top-level id), replacing what was there.
    NodeId mount(std::string_view slot, View view);
    // Mounts a view beneath every other slot: painted first, reached by the
    // pointer last (a window's own frame, under the application).
    NodeId mount_beneath(std::string_view slot, View view);
    void unmount(std::string_view slot);
    // The slot's view keeps `insets` from the window's edges (the
    // application inside a window's own frame); none by default.
    void set_slot_insets(std::string_view slot, Insets insets);
    [[nodiscard]] Insets slot_insets(std::string_view slot) const;
    [[nodiscard]] NodeId root() const noexcept;

    // ── the tree ──
    [[nodiscard]] bool valid(NodeId node) const noexcept;
    [[nodiscard]] NodeId find(std::string_view path) const;
    // The component of type C at a node — the program's own access to what
    // it mounted (a canvas's statistics for its HUD); null when the node is
    // not a C. Checked against C's type, not cast blindly (no RTTI needed).
    template <class C>
    [[nodiscard]] C* component(NodeId node) noexcept {
        return static_cast<C*>(instance_of(node, component_type<C>()));
    }
    [[nodiscard]] void* instance_of(NodeId node, const ComponentType& type) noexcept;
    [[nodiscard]] const void* instance_of(NodeId node, const ComponentType& type) const noexcept;
    [[nodiscard]] std::string path(NodeId node) const override;
    [[nodiscard]] NodeId parent(NodeId node) const override;
    [[nodiscard]] std::vector<NodeId> children(NodeId node) const override;
    [[nodiscard]] const ComponentType& type(NodeId node) const;
    [[nodiscard]] std::source_location origin(NodeId node) const;  // where its element was built
    [[nodiscard]] Value get(NodeId node, std::string_view property) const override;
    void set(NodeId node, std::string_view property, const Value& value) override;
    // A property read from a source from now on: a signal, an expression, two-way
    // where the source writes (what an element's property is; the C ABI's omuiBind).
    void bind(NodeId node, std::string property, Source source);
    // Handlers added at run time (scripts); returns a token for `off`.
    std::uint64_t on(NodeId node, std::string event, Handler handler) override;
    void off(std::uint64_t token) override;
    // Pre-order traversal from the root.
    void walk(FunctionRef<void(NodeId, int depth)> visit) const;
    [[nodiscard]] std::size_t node_count() const noexcept;
    // The node whose index is `index` (display-list node markers carry it).
    [[nodiscard]] NodeId node_at_index(std::uint32_t index) const;

    // ── frames ──
    FrameOutput frame(const FrameInput& input = {});
    [[nodiscard]] const render::DisplayList& display_list() const noexcept;
    void resize(Size size, float scale);
    [[nodiscard]] Size size() const noexcept;
    [[nodiscard]] float scale() const noexcept;
    [[nodiscard]] hanim::Clock& clock() noexcept;
    // Time control for tools (the animation inspector, time-travel
    // debugging): the application's time follows the host's clock until one
    // of these is used, and then the host's time mapped by them. Timers and
    // animations both follow it. Seeking re-evaluates every live animation at
    // the new time (animations are pure functions of time), backwards too.
    struct TimeState {
        hanim::seconds host{0};   // the host's clock
        hanim::seconds shown{0};  // what the application sees
        bool          controlled = false;
        bool          paused = false;
        double        rate = 1;
    };
    void pause_time(bool paused);
    void set_time_rate(double rate);
    void step_time(hanim::seconds dt);   // takes effect at the next frame
    void seek_time(hanim::seconds t);    // takes effect at the next frame
    void release_time();                // back to the host's time
    [[nodiscard]] TimeState time_state() const noexcept;
    // Frames drawn in the last second of real time (a frame with nothing to
    // draw is not one): what a HUD shows as its frame rate.
    [[nodiscard]] double frame_rate() const noexcept;
    // The last frames' statistics, oldest first (up to 240).
    [[nodiscard]] std::vector<FrameStats> frame_history() const;
    // The frame's pipeline and the stage plugins it is built from: tools
    // read them, and replace, add or remove stages between frames.
    [[nodiscard]] pipeline::Pipeline& frame_pipeline() noexcept;
    [[nodiscard]] pipeline::Registry& stage_plugins() noexcept;
    void set_debug(DebugOptions debug);
    [[nodiscard]] const DebugOptions& debug() const noexcept;
    // Everything the runtime knows of a node, for tools (`omui agent inspect`):
    // type, contract, where it was built, properties, layout style and frame,
    // bounds, state, transitions and handlers.
    [[nodiscard]] Value inspect(NodeId node) const;
    // When the earliest timer is due (hosts sleep until then); none when no timer waits.
    [[nodiscard]] std::optional<seconds> next_timer() const noexcept;
    [[nodiscard]] NodeId hit_test(Point window) const;

    // ── actions, focus, verbs ──
    // Performs an action of a node (pointers, keys, agents and scripts all
    // come here): S-1 effects and their answers (needs-answer), S-4 nodes
    // being removed, disabled nodes, the verb a node invokes. `call.args`
    // are the action's arguments; answers with an envelope.
    Envelope act(NodeId node, std::string_view action, const Invocation& call = {});
    // An action on an item a virtualising node holds without a node of its
    // own (its `act_item`): the item's action checked as a node's is —
    // effect, answer, arguments — and journaled.
    Envelope act_item(NodeId holder, std::string_view key, std::string_view action, const Invocation& call = {});
    // An item of a virtualising node, stated or not (its `virtual_item`).
    [[nodiscard]] std::optional<spec::SemanticNode> virtual_item(NodeId holder, std::string_view key) const;
    [[nodiscard]] bool acts_on_items(NodeId holder) const;
    [[nodiscard]] std::span<const ActionEntry> action_journal() const noexcept;
    // The actions a node offers now, with their effects.
    [[nodiscard]] std::vector<spec::ActionDesc> actions(NodeId node) const;
    // An application verb, as `act` performs an action: its checks (S-1,
    // S-10, S-11), a dry run, the journal; an irreversible or external one
    // ends what the window's history can undo (S-12).
    Envelope invoke(std::string_view verb, const Invocation& call = {});

    // ── history and transactions (S-12, S-13) ──
    // A reversible action whose implementation gives its inverse
    // (ActResult::undo) joins the history of its scope: the nearest node
    // around it (or itself) with `undo_scope`, else the window (the root).
    // `undo` and `redo` are actions of the scope's node, offered while there
    // is something to undo or redo; undoing performs the inverses as the one
    // undoing would perform them. An irreversible or external action is a
    // barrier: undo stops there (refused). A read action is not recorded; an
    // action that gives no inverse is not recorded either. Any new change
    // clears what could be redone.
    [[nodiscard]] NodeId undo_scope(NodeId node) const;
    [[nodiscard]] History history(NodeId scope) const;
    void clear_history(NodeId scope);
    Envelope undo(NodeId scope, const Invocation& call = {});
    Envelope redo(NodeId scope, const Invocation& call = {});
    // A transaction: the actions performed until it ends are one step of
    // history, all done or all taken back. Inside one, an irreversible or
    // external action is refused before it runs (nothing could take it
    // back), and so are undo and redo. Rolling back takes every step back,
    // last first. Transactions nest: the outermost one decides.
    void begin_transaction(std::string label);
    Envelope commit_transaction();
    Envelope rollback_transaction(const Invocation& call = {});
    [[nodiscard]] bool in_transaction() const noexcept;
    [[nodiscard]] NodeId focused() const noexcept;
    void focus_next(bool backward = false);
    [[nodiscard]] Verbs& verbs() noexcept override;
    [[nodiscard]] UiQueue& queue() noexcept;

    // ── semantics ──
    // The node as the semantic tree states it (contract ①).
    [[nodiscard]] spec::SemanticNode semantic_node(NodeId node) const;
    // Bumped whenever a node's semantics may have changed.
    [[nodiscard]] std::uint64_t semantic_revision() const noexcept;

    // ── theme ──
    void set_theme(Theme theme);
    void set_reduce_motion(bool reduce);
    [[nodiscard]] bool reduce_motion() const noexcept;
    void set_reduce_transparency(bool reduce);

    // ── Services ──
    [[nodiscard]] const Theme& theme() const override;
    [[nodiscard]] bool reduce_transparency() const override;
    [[nodiscard]] float pixel_scale() const override { return scale(); }
    [[nodiscard]] bool deterministic() const override;
    [[nodiscard]] FrameDemandLease demand_frames(FrameDemand d) override;
    void release_frames(std::uint64_t token) noexcept override;
    // What the last frame found frames are wanted for: animations, input in
    // the last 300 ms, timers, the leases (D-70).
    // The style's effects (they replace the rules before), and a cross-fade
    // from what is on screen now to what comes, over `length` (a style's or
    // a theme's change seen as one).
    void set_effect_rules(std::vector<EffectRule> rules);
    [[nodiscard]] const std::vector<EffectRule>& effect_rules() const noexcept;
    void crossfade(seconds length);
    [[nodiscard]] const FrameDemand& frame_demand() const noexcept;
    // The display's refresh rate, as the host last said (0: unknown).
    [[nodiscard]] double display_hz() const noexcept;
    [[nodiscard]] typeset::FontCollection& fonts() override;
    [[nodiscard]] render::ImageStore& images() override;
    [[nodiscard]] hanim::seconds now() const override;
    [[nodiscard]] bool time_paused() const override;
    [[nodiscard]] Rect bounds(NodeId node) const override;
    [[nodiscard]] Size content_size(NodeId node) const override;
    [[nodiscard]] Insets content_insets(NodeId node) const override;
    [[nodiscard]] NodeState state(NodeId node) const override;
    void emit(NodeId node, std::string_view event, const Value& args) override;
    void write_back(NodeId node, std::string_view property, const Value& value) override;
    void invalidate_paint(NodeId node) override;
    void invalidate_layout(NodeId node) override;
    void invalidate_semantics(NodeId node) override;
    void restyle(NodeId node) override;
    void recompose(NodeId node) override;
    Model& model(std::string_view name) override;
    [[nodiscard]] Signal<std::optional<Element>> outlet(std::string_view name) override;
    NodeId reveal(std::string_view path) override;
    // A virtualising node's items as the semantic tree states them (S-7).
    [[nodiscard]] std::vector<spec::SemanticNode> virtual_items(NodeId node) const;
    void request_frame() override;
    void scroll_into_view(NodeId node) override;
    void focus(NodeId node) override;
    void capture_pointer(NodeId node) override;
    void release_pointer() override;
    bool perform(NodeId node, std::string_view action, const Value& args) override;
    void text_input(NodeId node, std::optional<Rect> caret) override;
    [[nodiscard]] std::string clipboard_text() override;
    void set_clipboard_text(std::string_view text) override;
    hanim::Set& animations() override;
    std::uint64_t after(seconds delay, std::function<void()> fn) override;
    void cancel(std::uint64_t timer) override;

    // The text-input request of the last frame (hosts show the IME there).
    [[nodiscard]] std::optional<Rect> text_input_rect() const noexcept;
    // The pointer's shape over the hovered node: its `cursor` property, its
    // type's, or its ancestors' ("default" when none asks).
    [[nodiscard]] std::string_view cursor() const;
    // Where the clipboard is (a host's system clipboard); without one the
    // runtime keeps its own.
    void set_clipboard(std::function<std::string()> read, std::function<void(std::string_view)> write);
    // Opens an address outside the program (a document's link) where the
    // host can: the desktop's browser, a new tab on the Web. Only http,
    // https and mailto; false when refused or nothing opens it. Every
    // address asked for is kept, in order (`opened`: what a test reads).
    bool open_url(std::string_view url);
    void set_url_opener(std::function<bool(std::string_view)> open);
    [[nodiscard]] const std::vector<std::string>& opened() const noexcept;

    struct Impl;  // defined by the implementation (openmui.view:impl)

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
