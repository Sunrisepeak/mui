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
export module omui.view:runtime;

import std;
import omui.core;
import mlibs.layout;
import xrender.vector;
import mlibs.pipeline;
import mlibs.text;
import :element;
import :theme;
import :model;
import :component;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::FunctionRef;
}

export namespace omui {

// How much drawings move when they change (Runtime::set_motion_level).
enum class MotionLevel : std::uint8_t { full, reduced, off };

// How the frame is drawn here and now (performance tiers §4, §5, D-94): the
// device's tier (its host's), and what the frame budget allows — the
// governor lowers motion, then effects, when frames keep missing it, and
// raises them again when there is room. Recorded with a session (an input):
// a replay draws as the recording did.
struct Adaptation {
    mlibs::pipeline::Tier tier = mlibs::pipeline::Tier::t3;     // the device's
    MotionLevel    motion = MotionLevel::full;    // at most this (the person's own setting may be less)
    mlibs::pipeline::Tier effects = mlibs::pipeline::Tier::t4;  // effects drawn as on this tier, at most the device's
    std::string    reason;                        // why it is so now, said plainly
    friend bool operator==(const Adaptation&, const Adaptation&) = default;
};

// A node's drawing moving between two (Runtime::motions).
struct MotionState {
    NodeId      node;
    std::string path;
    double      progress = 0;   // its curve's, 0 to 1 (a spring may pass 1 on its way)
    double      start = 0, end = 0;
    std::size_t matched = 0, from_only = 0, to_only = 0, glyphs_kept = 0;
};

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
    mlibs::text::FontCollection* fonts = nullptr;  // nullptr: the runtime loads the default fonts
    hanim::Clock           clock = hanim::Clock::virtual_time();
    Size                  size{800, 600};   // logical pixels
    float                 scale = 1.0f;     // physical pixels per logical pixel
    bool                  reduce_motion = false;
    bool                  reduce_transparency = false;  // see-through materials (glass) drawn opaque
    bool                  agent_players = false;  // agents may join as players (Services::agent_players)
    Stages                stages = Stages::paint;
    bool                  complete_frames = false;  // every frame as at rest (Services::complete_frames): offline renders
    DebugOptions          debug;
    // The frame's pipeline (D-66): nullopt is runtime_pipeline(stages). A
    // custom one may replace, add or leave out stages; `plugins` adds the
    // stage plugins it names to the runtime's registry.
    std::optional<mlibs::pipeline::Description>            pipeline;
    std::function<void(mlibs::pipeline::Registry&)>        plugins;
    // Null preserves the legacy global fallback. Applications and embedded
    // providers bind an instance so caps, grants and sessions are isolated.
    std::shared_ptr<AgentAccess> access;
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
inline constexpr std::string_view display = "display"; // xrender::DisplayList
}  // namespace frame_resource

// A style's effect on what a selector names (D-69): "window" (the whole
// window), a component type ("button"), or "role:<role>" ("role:switch").
// `effect` is written as the pixel_effect property is: {name, params…}. A
// node's own pixel_effect wins.
struct EffectRule {
    std::string selector;
    Value       effect;
};

// Host-owned attribution for a half-open event interval. These are trusted
// C++ session handles, never decoded from event/request JSON. Intervals are
// ordered, nonempty, disjoint and bounded by FrameInput::events. Uncovered
// events are the person's; one frame still performs layout and paint once.
struct InputAttribution {
    std::size_t begin = 0, end = 0;
    Actor actor{"agent", "anonymous", {}};
    std::shared_ptr<const AgentSession> session;
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
    // Borrowed for the synchronous frame call; a host with one agent batch
    // can provide a stack array, without allocating a container each frame.
    std::span<const InputAttribution> attribution;
};
[[nodiscard]] std::expected<void, std::string> validate_input_attribution(const FrameInput& input);

}  // namespace omui

export namespace mlibs::pipeline {
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
struct resource_type<xrender::DisplayList> {
    static constexpr std::string_view name = "render.DisplayList";
};
}  // namespace mlibs::pipeline

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
    const xrender::DisplayList* list = nullptr;  // the runtime's list, valid until the next frame
    bool                       repainted = false;
    bool                       animating = false;  // another frame is wanted
    std::uint64_t              revision = 0;       // semantic revision after the frame
    FrameStats                 stats;
    std::optional<seconds>     next_timer;         // when the earliest timer is due (hosts sleep until then)
    FrameDemand                demand;             // how often frames are wanted from now on, and why (D-70)
    // Why a stage failed (the pipeline stopped there; logged too); empty
    // when every stage ran. A host may say so, retry, or give up.
    std::string                failure;
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
[[nodiscard]] xrender::Parallel& raster_parallel() noexcept;

// The runtime's frame pipeline for a level: H0 (semantics) is timers, input,
// regions and animations; H1 (layout) adds layout and bounds; H2 (paint)
// adds paint and the debug overlay. Its stage plugins are
// `openmui.view.<stage>`.
[[nodiscard]] mlibs::pipeline::Description runtime_pipeline(Stages stages);

// An explicitly selected local presentation layer, independent of business
// layout and style values. All drawable kinds and descendants use the same
// layer; ordinary component state feedback remains live beneath it.
enum class PresentationChannel : std::uint8_t { none=0, translation=1, rotation=2, scale=4, shear=8, transform=15, image=16 };
[[nodiscard]] constexpr PresentationChannel operator|(PresentationChannel a,PresentationChannel b) noexcept {
    return static_cast<PresentationChannel>(static_cast<unsigned>(a)|static_cast<unsigned>(b));
}
struct PresentationTarget {
    NodeId node;
    PresentationChannel channels=PresentationChannel::transform;
    mlibs::geom::Point anchor{.5f,.5f};
};
enum class PresentationError : std::uint8_t { invalid_node,invalid_configuration,occupied,busy,stale,invalid_pose,capacity,unsupported_frame,invalid_effect,stale_effect };
[[nodiscard]] constexpr std::string_view presentation_error_name(PresentationError error) noexcept {
    switch(error) {
        case PresentationError::invalid_node:return "invalid-node";
        case PresentationError::invalid_configuration:return "invalid-configuration";
        case PresentationError::occupied:return "occupied";
        case PresentationError::busy:return "busy";
        case PresentationError::stale:return "stale";
        case PresentationError::invalid_pose:return "invalid-pose";
        case PresentationError::capacity:return "capacity";
        case PresentationError::unsupported_frame:return "unsupported-frame";
        case PresentationError::invalid_effect:return "invalid-effect";
        case PresentationError::stale_effect:return "stale-effect";
    }return "unknown";
}
struct PresentationFailure { PresentationError code; std::size_t target=0; };
struct PresentationState {
    NodeId node;
    hanim::Transform2D pose;
    mlibs::geom::Point anchor;
    PresentationChannel channels=PresentationChannel::none;
    bool has_frame=false, applied=false;
    std::optional<PresentationError> error;
    std::size_t image_effects=0,image_parameters=0;
};
struct PresentationImageLayer {
    xrender::EffectRef effect;
    std::uint64_t generation=0;
    std::span<const float> parameters;
};
struct PresentationEffects {std::span<const PresentationImageLayer> layers;};
inline constexpr std::size_t presentation_max_image_layers=64,presentation_max_image_parameters=4096;
class PresentationLease {
public:
    PresentationLease() noexcept = default;
    PresentationLease(PresentationLease&&) noexcept;
    PresentationLease& operator=(PresentationLease&&) noexcept;
    PresentationLease(const PresentationLease&)=delete;
    PresentationLease& operator=(const PresentationLease&)=delete;
    ~PresentationLease();
    [[nodiscard]] bool attached() const noexcept;
    [[nodiscard]] std::span<const PresentationTarget> targets() const & noexcept {return targets_;}
    std::span<const PresentationTarget> targets() const && = delete;
    // All selected fields publish together. No time, playback or callback is
    // owned here. Unselected fields belong to their other declared writers.
    // Cold reservation for a selected node's image channel; existing values
    // remain live. Capacity is bounded and shared with this lease's lifetime.
    [[nodiscard]] std::expected<void,PresentationFailure> reserve_effects(std::size_t target,std::size_t layers,std::size_t parameters);
    void release_effects(std::size_t target) noexcept;
    // An empty effects span leaves image channels alone. Otherwise it has
    // one entry per target, and poses plus image chains publish atomically.
    [[nodiscard]] std::expected<void,PresentationFailure> publish(std::span<const hanim::Transform2D>,std::span<const PresentationEffects> = {});
    void release() noexcept;
private:
    friend class Runtime;
    class Runtime* runtime_=nullptr;
    std::uint64_t token_=0;
    std::weak_ptr<const void> life_;
    std::vector<PresentationTarget> targets_;
};

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
    // A region shows the slot (a view document's `slot`, a script's slot()):
    // what is mounted under the name goes there rather than the top level.
    [[nodiscard]] bool claimed(std::string_view slot) const;
    // The slot's view keeps `insets` from the window's edges (the
    // application inside a window's own frame); none by default.
    void set_slot_insets(std::string_view slot, Insets insets);
    [[nodiscard]] Insets slot_insets(std::string_view slot) const;
    [[nodiscard]] NodeId root() const noexcept;

    // ── the tree ──
    [[nodiscard]] bool valid(NodeId node) const noexcept;
    [[nodiscard]] NodeId find(std::string_view path) const override;
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
    // Where a script built it, `file:line:column` (development mode keeps it;
    // Live Design's design.locate), or empty.
    [[nodiscard]] std::string_view built_at(NodeId node) const;
    // The other way: the nodes a script built on a line of a file (its path as
    // the script was run by), in tree order.
    [[nodiscard]] std::vector<NodeId> built_on(std::string_view file, std::uint32_t line) const;
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
    [[nodiscard]] const xrender::DisplayList& display_list() const noexcept;
    [[nodiscard]] std::expected<PresentationLease,PresentationFailure> acquire_presentation(std::span<const PresentationTarget>);
    [[nodiscard]] std::optional<PresentationState> presentation(NodeId) const noexcept;
    void resize(Size size, float scale);
    // An external allocation change, scoped to this existing subtree. Unlike
    // model reordering, placement follows the new layout while content/state
    // keeps its motion. resize opens this transaction for the whole tree.
    // Ends after consequent layout and laid_out region rebuilds settle, not
    // after an animation/timer duration. Invalid boundaries are ignored.
    void constraints_changed(NodeId boundary);
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
    // A frame the host showed: after presenting it, the host says when and
    // what it cost (D-59). A frame with nothing new to show is not one.
    void frame_shown(ShownFrame f) noexcept;
    [[nodiscard]] const ShownFrames& shown_frames() const noexcept override;
    // Who draws the frames, as the host says ("cpu", "webgpu", "webgl", "vulkan"…).
    void set_renderer(std::string name);
    // Who gives the input of the frames that follow, when it is not the
    // person at the machine: an agent giving input as their devices would
    // (the agent protocol's `input`). The actions that input performs are
    // the agent's (openas: its actor, via the input), journaled as theirs
    // and decided as their requests over the bus are: the person's own
    // actions refused (S-10), the cap and the grants (S-11), the answer an
    // irreversible one needs. None: the person again.
    void set_input_actor(std::optional<Actor> actor, std::shared_ptr<const AgentSession> session = {});
    void set_agent_access(std::shared_ptr<AgentAccess> access);
    [[nodiscard]] AgentAccess& agent_access() noexcept;
    [[nodiscard]] const AgentAccess& agent_access() const noexcept;
    [[nodiscard]] std::shared_ptr<AgentAccess> agent_access_binding() const noexcept;
    [[nodiscard]] std::string_view renderer() const noexcept override;
    // The frame's pipeline and the stage plugins it is built from: tools
    // read them, and replace, add or remove stages between frames.
    [[nodiscard]] mlibs::pipeline::Pipeline& frame_pipeline() noexcept;
    [[nodiscard]] mlibs::pipeline::Registry& stage_plugins() noexcept;
    void set_debug(DebugOptions debug);
    [[nodiscard]] const DebugOptions& debug() const noexcept;
    // Everything the runtime knows of a node, for tools (`omui agent inspect`):
    // type, contract, where it was built, properties, layout style and frame,
    // bounds, state, transitions and handlers.
    [[nodiscard]] Value inspect(NodeId node) const;
    // When the earliest timer is due (hosts sleep until then); none when no timer waits.
    [[nodiscard]] std::optional<seconds> next_timer() const noexcept;
    [[nodiscard]] NodeId hit_test(Point window) const;
    [[nodiscard]] NodeId control_at(Point window) const override;

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
    [[nodiscard]] std::span<const ActionEntry> action_journal() const noexcept override;

    // The interaction recorder (D-148): development mode keeps everything
    // (full); otherwise errors, unless the application says otherwise.
    // Live Design's presentation (D-149): for `duration` from now, a design
    // step is seen over the application's own motion, as its kind says
    // (design_presentation); what did not change does not move, and the
    // person's motion level still holds.
    void present_design(seconds duration = seconds(0.9));
    // How the next design steps are presented (W14 step 4, designer-set):
    //   edit       what it adds grows in — written and unfolded from its top,
    //              a parent before what it holds; what it removes dissolves;
    //              what it changes springs, the nearest the change first;
    //   undo       played back: what the undone step made unwrites and what it
    //              took writes back, each the last first;
    //   switch_to  the shared elements (the same id, or found again by role
    //              and name) carried from where they were to where they are;
    //              the rest fades in and out.
    // `pace` scales its time (0.25 … 4; 2 is twice as fast).
    struct DesignPresentation {
        enum class Kind : std::uint8_t { edit, undo, switch_to };
        Kind   kind = Kind::edit;
        double pace = 1;
    };
    void set_design_presentation(DesignPresentation presentation);
    [[nodiscard]] DesignPresentation design_presentation() const noexcept;
    void set_record_level(RecordLevel level);
    [[nodiscard]] RecordLevel record_level() const noexcept;
    // What was recorded of the input events after `since` (a cause; 0: all
    // that is kept — the last 4096 records), oldest first.
    [[nodiscard]] std::vector<InteractionRecord> interactions(std::uint64_t since = 0) const;
    // Every frame since the recorder kept everything (full: development
    // mode) — its time and the input it was given — as a scenario a fresh
    // instance replays (`--replay`, W12): {"omui", "kind": "input", "size",
    // "frames": [{"t", "events"?}…]}. What a person typed is in it: it is
    // kept only at full, and read as the person's records are.
    [[nodiscard]] Value input_scenario() const;
    // A press and what came of it: from the pointer's press to its release,
    // the records and the actions they led to, and what it comes to in a
    // sentence ("activated main/save", "released outside main/save …").
    struct Explanation {
        std::uint64_t                  cause = 0;  // the press's
        std::vector<InteractionRecord> records;
        std::vector<ActionEntry>       actions;
        std::string                    summary;
    };
    // The last press within a few pixels of `at`, or the one numbered
    // `cause`; nothing when none is recorded.
    [[nodiscard]] std::optional<Explanation> explain(std::optional<Point> at, std::uint64_t cause = 0) const;
    [[nodiscard]] NodeId undoes(std::uint64_t entry) const override;
    bool undo_entry(std::uint64_t entry, const Actor* actor) override;
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
    // Inside an action's handlers — a button's on_activated, a verb's
    // function, what a change of value calls — how the program takes back
    // what it just did, and does it again: the action joins its scope's
    // history (undo runs `undo`, redo runs `redo`), as an action whose
    // component gives its inverse does. A reversible action that changes
    // the program's own state says how here, or undo passes over it.
    // Outside an action, nothing.
    void taken_back_by(std::function<void()> undo, std::function<void()> redo);
    // The person takes over from every agent (spec S-14): until they hand
    // back, each change an agent asks for — an action, a verb, undo or redo,
    // a transaction, a preview — is refused with `conflict` (no retry_after:
    // the person decides when), and nothing it asked for takes effect after;
    // reading stays open to it, and what it did stays in the history, to be
    // undone a step at a time. The person's own verbs `agents.take_over` and
    // `agents.hand_back` do the same from a control (an agent cannot: S-10).
    void take_over();
    void hand_back();
    [[nodiscard]] bool taken_over() const override;
    // An agent says what it intends, or hands back (AgentNote): the one note
    // an actor has is replaced (empty text: withdrawn); its presence shows it.
    void note(AgentNote note);
    [[nodiscard]] std::span<const AgentNote> agent_notes() const noexcept override;
    // Claims (D-160): an actor holding a part of the interface while it designs it, so another
    // agent's change there is refused (`conflict`: who holds it, what for, for how long). Two
    // claims stand together unless they meet: the same node (the node itself, or the same
    // property), or one's subtree holding the other's node. The same actor claiming the same
    // part again renews it. A person is never held; `by` a person releases any claim.
    std::expected<Claim, Claim> claim(const ClaimRequest& request, const Actor& by);  // error: the claim in the way
    bool release_claim(std::string_view node, std::string_view property, const Actor& by);
    [[nodiscard]] std::vector<Claim> claims() const override;
    // The claim another than `actor` holds over this node (itself, a property of it, or a subtree around it), if one.
    [[nodiscard]] std::optional<Claim> claim_over(NodeId node, std::string_view actor) const;
    // Who performs the action under way (its handlers run inside it): what
    // an action the program performs in turn carries on as its actor
    // (openas: an agent's click that takes a card takes it as the agent).
    // None outside an action.
    [[nodiscard]] std::optional<Actor> acting() const;
    // Trusted host code may retain the current authority for a deferred
    // continuation. Resubmit through invoke/act: permissions are checked
    // again, and revoked sessions remain revoked. Never accept this from JSON.
    [[nodiscard]] std::optional<Invocation> current_invocation() const;
    // The press under way: its number (each press of a pointer counts one),
    // none while no pointer is held. What a drag changes, step by step, is one
    // gesture: a program's own history joins its steps by it (a slider
    // dragged twice is two steps, however quick).
    [[nodiscard]] std::optional<std::uint64_t> pointer_gesture() const;
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
    // A preview (Part 2 §4.1, A: shown before it is allowed): until
    // end_preview nothing done is journaled and nothing drawn moves (motion
    // off: a change shows its end at once), so a transaction run and rolled
    // back inside one is seen only by whoever looks between — the picture of
    // what an agent's actions would make, with nothing left of them after.
    // Previews nest; the outermost ends it.
    void begin_preview();
    void end_preview();
    [[nodiscard]] bool previewing() const noexcept;
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
    // A node's text as laid out (a text field's): none when it has none to say.
    [[nodiscard]] std::optional<TextLayout> text_layout(NodeId node) const;
    // Bumped whenever a node's semantics may have changed.
    [[nodiscard]] std::uint64_t semantic_revision() const noexcept;

    // ── theme ──
    void set_theme(Theme theme);
    // Its fonts changed (one arrived later: FontCollection::add_fallback):
    // every text shapes and lays out again, drawn in what it has now.
    void fonts_changed();
    // A reader needs every face the host deferred (another language's: a
    // reference frame drawn in it): the host fetches them, as it does when
    // text first has a glyph no face it has covers. Taken once by the host.
    void want_fonts() noexcept;
    [[nodiscard]] bool take_font_request() noexcept;
    // Theme scopes: a subtree with a theme and effect rules of its own — a
    // style pack previewed inside a page that keeps its own. A node joins the
    // scope its `theme_scope` property names, and its descendants with it
    // (overlays it opens too); the nearest scope wins, whole: its theme and
    // its rules replace the runtime's, nothing merges. A name not registered
    // leaves the node in its parent's. "window" rules apply to the node that
    // joins the scope. Setting a registered scope again restyles its nodes.
    void set_scope(std::string_view name, Theme theme, std::vector<EffectRule> rules = {});
    void remove_scope(std::string_view name);
    [[nodiscard]] const Theme* scope_theme(std::string_view name) const;
    [[nodiscard]] const std::vector<EffectRule>* scope_rules(std::string_view name) const;
    // The theme the node is drawn in: its scope's, else the runtime's.
    [[nodiscard]] const Theme& theme_of(NodeId node) const;
    void set_reduce_motion(bool reduce);
    [[nodiscard]] bool reduce_motion() const noexcept;
    // How much the drawing moves when it changes (universal motion, D-93; the
    // user's setting where the application exposes it, D-94): full, reduced
    // (short tweens, no stagger) or off. Reduced motion asked by the system is off.
    void set_motion_level(MotionLevel level);
    [[nodiscard]] MotionLevel motion_level() const noexcept;
    // Where the application is within itself — a page's place, `zh/features`
    // — for hosts with an address to show it (the Web: the URL's hash, a
    // step of the browser's history) and to set it when a person goes back
    // or forward. The application sets it as it goes, and follows it when
    // it changes otherwise (route_revision says when).
    void set_route(std::string route);
    [[nodiscard]] const std::string& route() const noexcept;
    [[nodiscard]] std::uint64_t route_revision() const noexcept;
    // The person's choice among the motions a slot offers (universal motion
    // §5.2: a setting the application exposes — "simple", "liquid", "ink"):
    // a slot whose candidates include it moves by it while it fits the
    // device; others, as they would. Empty: none.
    void set_motion_choice(std::string name);
    [[nodiscard]] const std::string& motion_choice() const noexcept;
    // The morphs under way: whose, how far, how much of the two drawings was matched.
    [[nodiscard]] std::vector<MotionState> motions() const;
    // A hold on a node's morph under way (universal motion §6.2: a tool's —
    // the motion studio, a time knob — or a program's): its own time paused,
    // sought, reversed, slowed. Held, it is kept when it rests, until
    // released (then it goes on from where it is, at the runtime's pace). A
    // node with no morph under way: false, nothing held.
    bool hold_motion(NodeId node);
    void release_motion(NodeId node);
    void pause_motion(NodeId node);
    void play_motion(NodeId node);
    void seek_motion(NodeId node, double seconds);  // since its start: 0 the first drawing, its length the second
    void set_motion_rate(NodeId node, double rate);  // negative: backwards
    // Its length (seconds until it rests) and its own time now; nullopt with no morph under way.
    [[nodiscard]] std::optional<std::pair<double, double>> motion_time(NodeId node) const;
    // The device's tier, as its host finds it (the GPU it has, the Web, an MCU).
    void set_device_tier(mlibs::pipeline::Tier tier, std::string reason);
    [[nodiscard]] const Adaptation& adaptation() const noexcept;
    // As a session recorded it, or a tool sets it: the governor's say set aside until it is turned on again.
    void set_adaptation(const Adaptation& a);
    // The frame-budget governor (performance tiers §4.2): on where frames are
    // shown to a person (the desktop's and the Web's hosts); off in tests,
    // films and replays, where the frames' cost must not change what is drawn.
    void set_governor(bool on);
    // The host's measure of the last frame's whole cost (milliseconds, its
    // raster and present included): what the governor weighs.
    void report_frame_cost(double ms);
    void set_reduce_transparency(bool reduce);

    // ── Services ──
    [[nodiscard]] const Theme& theme() const override;
    [[nodiscard]] bool reduce_transparency() const override;
    // Agents may join as players (the application allows, the person agrees).
    void set_agent_players(bool allowed);
    [[nodiscard]] bool agent_players() const override;
    [[nodiscard]] bool motion_reduced() const override;
    [[nodiscard]] float pixel_scale() const override { return scale(); }
    [[nodiscard]] bool deterministic() const override;
    [[nodiscard]] bool complete_frames() const override;
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
    // Only the scope's nodes' area (a scoped style's change).
    void crossfade(seconds length, std::string_view scope);
    [[nodiscard]] const FrameDemand& frame_demand() const noexcept;
    // The display's refresh rate, as the host last said (0: unknown).
    [[nodiscard]] double display_hz() const noexcept;
    [[nodiscard]] mlibs::text::FontCollection& fonts() override;
    [[nodiscard]] xrender::ImageStore& images() override;
    [[nodiscard]] hanim::seconds now() const override;
    [[nodiscard]] bool time_paused() const override;
    [[nodiscard]] Rect bounds(NodeId node) const override;
    // The sampled border box actually presented by the last painted frame,
    // including drawing morphs and ancestor transforms. This is independent
    // of ink (transparent controls still have a frame). Before first paint or
    // in a layout-only pipeline, visual layout transforms are the fallback.
    [[nodiscard]] Rect drawn_bounds(NodeId node) const override;
    [[nodiscard]] std::optional<Point> pointer() const override;
    void watch_pointer(NodeId node) override;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> text_selection(NodeId node) const override;
    void select_text(NodeId node, std::size_t anchor, std::size_t focus) override;
    // The selected text (each node's part, a line apart), and none selected again.
    [[nodiscard]] std::string selected_text() const;
    void clear_text_selection();
    // Where it is on the window as set: through its own and its ancestors'
    // transforms as they are set, not part way through their transitions —
    // a canvas's target node where the camera will show it. Kept as a target
    // API: semantics and real pointer input use the current presented frame.
    [[nodiscard]] Rect screen_bounds(NodeId node) const;
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
    void moving(NodeId node) override;
    void scroll_into_view(NodeId node) override;
    void scroll_to_start(NodeId node) override;
    void focus(NodeId node) override;
    // The focus moved where a person's keys or assistive technology asked
    // (a page's skip link): its ring drawn.
    void focus_visibly(NodeId node);
    void capture_pointer(NodeId node) override;
    void release_pointer() override;
    bool perform(NodeId node, std::string_view action, const Value& args) override;
    void performed(NodeId node, std::string_view action, const Value& args, std::vector<Inverse> inverse) override;
    void changed_outside_history(NodeId node) override;
    void text_input(NodeId node, std::optional<Rect> caret) override;
    [[nodiscard]] std::string clipboard_text() override;
    void set_clipboard_text(std::string_view text) override;
    hanim::Engine& engine() override;
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
    bool open_url(std::string_view url) override;
    std::uint64_t open_web(std::string_view url, std::function<void(const WebPage&)> changed) override;
    void place_web(std::uint64_t page, Rect bounds, bool shown) override;
    bool command_web(std::uint64_t page, std::string_view command, std::string_view argument) override;
    void close_web(std::uint64_t page) override;
    [[nodiscard]] bool focusable(NodeId node) const override;
    void set_url_opener(std::function<bool(std::string_view)> open);
    [[nodiscard]] const std::vector<std::string>& opened() const noexcept;
    // A file given to the person (an export: a picture, a document) where
    // the host can: the Web's download, the desktop's Downloads folder;
    // false when nothing takes it. The last files offered are kept, in order
    // (`offered`: what a test reads; at most offered_kept).
    struct OfferedFile {
        std::string name, type, bytes;  // its name, its media type, its contents
    };
    static constexpr std::size_t offered_kept = 16;
    bool offer_file(OfferedFile file);
    void set_file_offer(std::function<bool(const OfferedFile&)> offer);
    [[nodiscard]] const std::deque<OfferedFile>& offered_files() const noexcept;
    // The system's own surfaces where the host has them (W7, D-140): a file
    // chooser, a notification, a tray icon's menu. Each answers false when it
    // is not there after all; what is missing is drawn by the interface
    // instead (the SDK's System), the same on every platform.
    struct FileFilter {
        std::string name;     // "Pictures"
        std::string pattern;  // "png;jpg": extensions, `;`-separated ("*": all)
    };
    struct FileChoice {
        enum class Kind : std::uint8_t { open, save, folder };
        Kind                    kind = Kind::open;
        std::string             title;
        std::vector<FileFilter> filters;
        bool                    many = false;  // open: several files
        std::filesystem::path   start;         // where it begins (default: the working directory)
        std::string             name;          // save: the name offered
    };
    using FilesChosen = std::function<void(std::vector<std::filesystem::path>)>;  // none: cancelled
    struct Notice {
        std::string title, body;
        std::string verb;  // pressed: the application's verb it invokes (none: it only tells)
    };
    struct MenuEntry {
        std::string label;
        std::string verb;  // what choosing it invokes
        bool        checked = false;
    };
    struct NativeSurfaces {
        // `chosen` may be called on another thread: post it (queue()).
        std::function<bool(const FileChoice&, FilesChosen chosen)> choose_files;
        // `pressed`: the person chose the notice (its verb's to run); may be called on another thread.
        std::function<bool(const Notice&, std::function<void()> pressed)> notify;
        std::function<bool(const std::vector<MenuEntry>&, std::string_view tooltip, std::function<void(std::string_view verb)> chosen)> tray;
        // Web pages in the system's own view (D-141), placed over a node: open answers the page's
        // number (0: none here), `changed` may be called on another thread (the runtime posts it).
        std::function<std::uint64_t(std::string_view url, std::function<void(const WebPage&)> changed)> web_open;
        std::function<void(std::uint64_t page, Rect bounds, bool shown)>                             web_place;
        std::function<bool(std::uint64_t page, std::string_view command, std::string_view argument)> web_command;
        std::function<void(std::uint64_t page)>                                                      web_close;
    };
    void set_native_surfaces(NativeSurfaces surfaces);
    [[nodiscard]] const NativeSurfaces& native_surfaces() const noexcept;
    // What is drawn now as SVG (each node's drawing marked by its path), over
    // `region` (logical pixels; the window when none), where the host can say
    // it in vectors (agent::vector_capture); why not, otherwise.
    [[nodiscard]] std::expected<std::string, std::string> capture_svg(std::optional<Rect> region = {});
    void set_svg_capture(std::function<std::expected<std::string, std::string>(Rect)> capture);
    // What is drawn now as PNG, over `region` (logical pixels; the window
    // when none), drawn by the host's renderer; why not, where it draws none.
    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> capture_png(std::optional<Rect> region = {});
    void set_png_capture(std::function<std::expected<std::vector<std::uint8_t>, std::string>(Rect)> capture);
    // What the application was made from, reloaded at once where it can (a
    // player: its scripts, the modules they import, views, components and
    // themes): what changed on disk, or the files `request` names
    // ({"paths": […]}); {"last": true}, what the last reload did (an
    // automatic one too). The answer: what it reloaded, how long it took, the
    // semantic changes, the problems. The protocol's `reload` adds the pixels
    // and the motion (W2). Why not, where nothing reloads.
    [[nodiscard]] std::expected<Value, std::string> reload_sources(const Value& request);
    void set_source_reloader(std::function<std::expected<Value, std::string>(const Value&)> reloader);
    // The person's own agent (agent connect, D-95), where the host can link
    // one — the Web, through a bridge on the person's machine: link_agent()
    // starts looking for it and answers the pairing code the person gives
    // their agent (the same code while it still looks); empty where the
    // host cannot. The host says how it links, and each state it hears.
    std::string link_agent() override;
    void unlink_agent() override;
    [[nodiscard]] const AgentLink& agent_link() const noexcept override;
    [[nodiscard]] std::uint64_t agent_link_revision() const noexcept;
    struct AgentLinker {
        std::function<std::string()> link;
        std::function<void()>        unlink;
    };
    void set_agent_linker(AgentLinker linker);
    void agent_link_changed(AgentLink link);
    void follow_actions(NodeId node, bool follow) override;
    void point_out(NodeId by, std::string_view path) override;
    [[nodiscard]] const std::string& pointed_out() const noexcept;

    struct Impl;  // defined by the implementation (openmui.view:impl)

private:
    friend class PresentationLease;
    [[nodiscard]] bool owns_presentation(const PresentationLease&) const noexcept;
    [[nodiscard]] std::expected<void,PresentationFailure> reserve_presentation_effects(PresentationLease&,std::size_t,std::size_t,std::size_t);
    void release_presentation_effects(PresentationLease&,std::size_t) noexcept;
    [[nodiscard]] std::expected<void,PresentationFailure> publish_presentation(const PresentationLease&,std::span<const hanim::Transform2D>,std::span<const PresentationEffects>);
    void release_presentation(const PresentationLease&) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
