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
import openmui.render;
import openmui.text;
import :element;
import :theme;
import :component;

export namespace omui {

// How far a frame goes: the semantic tree alone (H0: build and reconcile),
// with layout (H1: bounds, hit testing), or to a display list (H2 and up).
enum class Stages : std::uint8_t { semantics, layout, paint };

struct RuntimeOptions {
    Theme                 theme = Theme::light();
    typeset::FontCollection* fonts = nullptr;  // nullptr: the runtime loads the default fonts
    anim::Clock           clock = anim::Clock::virtual_time();
    Size                  size{800, 600};   // logical pixels
    float                 scale = 1.0f;     // physical pixels per logical pixel
    bool                  reduce_motion = false;
    Stages                stages = Stages::paint;
};

struct FrameInput {
    std::optional<anim::seconds> time;   // virtual and render clocks: the frame's time
    std::vector<InputEvent>      events;
};

struct FrameStats {
    std::size_t nodes = 0;
    std::size_t laid_out = 0;    // layout nodes computed (not from cache)
    std::size_t painted = 0;     // nodes painted (0 when the list was reused)
    std::size_t bindings = 0;    // property bindings re-evaluated
    std::size_t animating = 0;   // animation channels still moving
};

struct FrameOutput {
    const render::DisplayList* list = nullptr;  // the runtime's list, valid until the next frame
    bool                       repainted = false;
    bool                       animating = false;  // another frame is wanted
    std::uint64_t              revision = 0;       // semantic revision after the frame
    FrameStats                 stats;
    std::optional<seconds>     next_timer;         // when the earliest timer is due (hosts sleep until then)
};

// One performed action, for audit (openas: who did what, with what result).
struct ActionEntry {
    std::string path;
    std::string action;
    std::string actor;
    int         exit_code = 0;
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
    void unmount(std::string_view slot);
    [[nodiscard]] NodeId root() const noexcept;

    // ── the tree ──
    [[nodiscard]] bool valid(NodeId node) const noexcept;
    [[nodiscard]] NodeId find(std::string_view path) const;
    [[nodiscard]] std::string path(NodeId node) const override;
    [[nodiscard]] NodeId parent(NodeId node) const override;
    [[nodiscard]] std::vector<NodeId> children(NodeId node) const override;
    [[nodiscard]] const ComponentType& type(NodeId node) const;
    [[nodiscard]] std::source_location origin(NodeId node) const;  // where its element was built
    [[nodiscard]] Value get(NodeId node, std::string_view property) const override;
    void set(NodeId node, std::string_view property, const Value& value) override;
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
    [[nodiscard]] anim::Clock& clock() noexcept;
    // When the earliest timer is due (hosts sleep until then); none when no timer waits.
    [[nodiscard]] std::optional<seconds> next_timer() const noexcept;
    [[nodiscard]] NodeId hit_test(Point window) const;

    // ── actions, focus, verbs ──
    // Performs an action of a node (pointers, keys, agents and scripts all
    // come here): S-1 effects and their answers (needs-answer), S-4 nodes
    // being removed, disabled nodes, the verb a node invokes. `call.args`
    // are the action's arguments; answers with an envelope.
    Envelope act(NodeId node, std::string_view action, const Invocation& call = {});
    [[nodiscard]] std::span<const ActionEntry> action_journal() const noexcept;
    // The actions a node offers now, with their effects.
    [[nodiscard]] std::vector<spec::ActionDesc> actions(NodeId node) const;
    [[nodiscard]] NodeId focused() const noexcept;
    void focus_next(bool backward = false);
    [[nodiscard]] Verbs& verbs() noexcept;
    [[nodiscard]] UiQueue& queue() noexcept;

    // ── semantics ──
    // The node as the semantic tree states it (contract ①).
    [[nodiscard]] spec::SemanticNode semantic_node(NodeId node) const;
    // Bumped whenever a node's semantics may have changed.
    [[nodiscard]] std::uint64_t semantic_revision() const noexcept;

    // ── theme ──
    void set_theme(Theme theme);
    void set_reduce_motion(bool reduce);

    // ── Services ──
    [[nodiscard]] const Theme& theme() const override;
    [[nodiscard]] typeset::FontCollection& fonts() override;
    [[nodiscard]] render::ImageStore& images() override;
    [[nodiscard]] anim::seconds now() const override;
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
    void request_frame() override;
    void focus(NodeId node) override;
    void capture_pointer(NodeId node) override;
    void release_pointer() override;
    void text_input(NodeId node, std::optional<Rect> caret) override;
    [[nodiscard]] std::string clipboard_text() override;
    void set_clipboard_text(std::string_view text) override;
    anim::Set& animations() override;
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

    struct Impl;  // defined by the implementation (openmui.view:impl)

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui
