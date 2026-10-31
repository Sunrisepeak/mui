// openmui.accessibility:map — the semantic tree as assistive technology reads
// it, without a platform: what each node becomes (AccessKit's model of a
// node, which every platform adapter then serves), what changed since it was
// last read, and the actions a screen reader asks for, performed on the tree.
//
// A node keeps its id as a 64-bit key (FNV-1a of the path, so a node is the
// same node from one update to the next); the root is the window, named as
// the window is. Roles are the spec's own mapping (roles.toml's `accesskit`).
// Static text says its words as its value (AccessKit reads a label's name
// from it); a slider its number and range; a checkbox or switch whether it is
// on; a heading its level; a status or timer is a live region.
//
// Requests arrive from the platform's threads: they are queued and performed
// on the runtime's thread by `serve`, as the person's own input (an actor of
// kind person, not remote: an assistive device is the person's hands, so what
// would ask a pointer for its answer is answered as a click answers it).
export module openmui.accessibility:map;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;

export namespace omui::accessibility {

// AccessKit's actions, in its order (native code holds the two together).
enum class Action : std::uint8_t {
    click,
    focus,
    blur,
    collapse,
    expand,
    custom,
    decrement,
    increment,
    hide_tooltip,
    show_tooltip,
    replace_selected_text,
    scroll_down,
    scroll_left,
    scroll_right,
    scroll_up,
    scroll_into_view,
    scroll_to_point,
    set_scroll_offset,
    set_text_selection,
    set_sequential_focus_navigation_starting_point,
    set_value,
    show_context_menu,
};
inline constexpr std::size_t action_count = 22;

enum class Toggled : std::uint8_t { none, off, on, mixed };
enum class Live : std::uint8_t { off, polite, assertive };

// A node as AccessKit models one.
struct Node {
    std::uint64_t              key = 0;
    std::string_view           role = "GenericContainer";  // AccessKit's name for it (static storage)
    std::string                label, description, value, url;
    std::optional<double>      number, min, max, step;
    std::optional<double>      scroll_x, scroll_x_max, scroll_y, scroll_y_max;
    std::optional<std::size_t> level;
    std::optional<bool>        selected, expanded;
    Toggled                    toggled = Toggled::none;
    Live                       live = Live::off;
    bool disabled = false, readonly = false, required = false, invalid = false, busy = false, hidden = false,
         modal = false, multiselectable = false, visited = false;
    std::uint32_t              actions = 0;       // bits of Action
    // The node's actions AccessKit has no name for (dismiss, a second of
    // activate / toggle / select): custom actions, their id the index.
    std::vector<std::string>   custom;
    std::optional<spec::Rect>  bounds;            // logical pixels, in the window
    std::vector<std::uint64_t> children;

    [[nodiscard]] bool has(Action a) const noexcept { return (actions >> static_cast<unsigned>(a)) & 1u; }
    void add(Action a) noexcept { actions |= 1u << static_cast<unsigned>(a); }
};

// The 64-bit key of a node id (never 0).
[[nodiscard]] std::uint64_t key_of(std::string_view id) noexcept;
// What a semantic node is to assistive technology (its children's keys from
// their ids; `key_of` names them).
[[nodiscard]] Node describe(const spec::SemanticNode& node);
// The openmui action an AccessKit action means on this node ("" none), with
// the order click prefers: activate, toggle, select.
[[nodiscard]] std::string_view click_action(const spec::SemanticNode& node) noexcept;

// What a screen reader (or voice control, or a switch) asks of a node.
struct Request {
    std::uint64_t              target = 0;
    Action                     action = Action::click;
    std::int32_t               custom = -1;   // for Action::custom
    std::optional<std::string> text;          // set_value, replace_selected_text
    std::optional<double>      number;        // set_value
    std::optional<Point>       point;         // set_scroll_offset, scroll_to_point (logical pixels)
    bool                       page = false;  // scroll_*: by a page, else by a line
};

struct MirrorOptions {
    std::string name;  // the window's name (its title); empty: the application's
};

// The tree as the platform was last told it, kept for adapters: their
// activation may come on another thread, and reads it under `lock`.
class Mirror {
public:
    explicit Mirror(Runtime& runtime, MirrorOptions options = {});

    // Reads what changed since the last call (on the runtime's thread):
    // the nodes to send, which also hold every node whose children changed.
    // Empty when nothing did.
    struct Changes {
        std::vector<Node> nodes;
        std::uint64_t     focus = 0;
        bool              focus_moved = false;
        [[nodiscard]] bool empty() const noexcept { return nodes.empty() && !focus_moved; }
    };
    Changes refresh();
    void    rename(std::string name);

    // The whole tree (for an adapter's activation; any thread).
    void each(const std::function<void(const Node&)>& fn) const;
    [[nodiscard]] std::uint64_t root() const noexcept { return root_; }
    [[nodiscard]] std::uint64_t focus() const;
    [[nodiscard]] std::size_t   size() const;
    [[nodiscard]] std::optional<std::string> id_of(std::uint64_t key) const;

    // A request from any thread; `wake`, if set, is called after it waits.
    void queue(Request request);
    void set_wake(std::function<void()> wake);
    // The waiting requests, performed (on the runtime's thread). Answers
    // what each answered, in order.
    std::vector<Envelope> serve();
    // One request, now.
    Envelope perform(const Request& request);
    [[nodiscard]] std::size_t waiting() const;

    [[nodiscard]] semantic::Tree& tree() noexcept { return tree_; }

private:
    Node root_node(const spec::SemanticNode* app) const;

    semantic::Tree                                      tree_;
    MirrorOptions                                       options_;
    std::uint64_t                                       root_ = 0;
    std::string                                         app_id_;  // the semantic root ("app")
    mutable std::mutex                                  mutex_;
    std::unordered_map<std::uint64_t, Node>             nodes_;
    std::unordered_map<std::uint64_t, std::string>      ids_;
    std::uint64_t                                       focus_ = 0;
    std::vector<Request>                                requests_;
    std::function<void()>                               wake_;
};

}  // namespace omui::accessibility
