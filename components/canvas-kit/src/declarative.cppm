// openmui.ui.canvas_kit:declarative — behaviours as data
// (view-data-files §5): a statechart in a *.behavior.omui.toml, run by C++.
//
//     # group-drag.behavior.omui.toml
//     [behavior.group-drag]
//     on      = "canvas"
//     initial = "idle"
//
//     [behavior.group-drag.states.idle]
//     on.pointer_down = { when = "hit != null && hit.kind == 'group.label'", to = "dragging", do = ["capture()"] }
//
//     [behavior.group-drag.states.dragging]
//     on.pointer_move = { do = ["move(hit.node, delta.world)"] }
//     on.pointer_up   = { to = "idle", do = ["commit('group.moved', hit.node)"] }
//
//     [behavior.group-drag.actions.nudge]          # what it adds to a node's actions
//     when = "node.kind == 'group.label'"
//     args = { dx = "f64", dy = "f64" }
//     do   = ["move(node.key, {'x': args.dx, 'y': args.dy})"]
//
// A state's `on` maps events — pointer_down, pointer_move, pointer_up,
// pointer_cancel, tap, key — to a transition or a list of them (the first
// whose `when` holds is taken); a transition may go `to` another state and
// `do` operations; a state may have `entry` and `exit` operations. `when`
// and the operations' arguments are expressions (view-data-files §4) over
// what the event says (`pointer`, `press`, `delta`, `travel`, `hit`, `over`,
// `mods`, `key`) and the canvas (`selection`, `zoom`, `vars`, `state`). The
// operations are the canvas's (behavior_operations()); everything is checked
// when the file loads — a misspelt state, event, operation or name is a
// problem with its line, not a surprise later. Nothing here runs a script:
// the expressions are evaluated by C++, so the per-frame path stays native.
export module openmui.ui.canvas_kit:declarative;

import std;

export namespace omui::world {

struct BehaviorProblem {
    std::string file;
    int         line = 0;
    int         column = 0;
    std::string message;
    std::string hint;

    // file:line:column: message (hint)
    [[nodiscard]] std::string text() const;
};

using BehaviorProblems = std::vector<BehaviorProblem>;

// Reads a behaviour file and registers what it declares (by name: again
// replaces it; canvases made after use the new one). Answers the names.
[[nodiscard]] std::expected<std::vector<std::string>, BehaviorProblems> load_behaviors(std::string_view text, std::string_view file = "<behaviour>");
[[nodiscard]] std::expected<std::vector<std::string>, BehaviorProblems> load_behavior_file(const std::filesystem::path& file);

// What a declarative behaviour can do and read: its operations with their
// signatures, and the names its expressions see.
struct BehaviorOperation {
    std::string_view name;
    std::string_view signature;  // "move(target, delta)"
    std::string_view doc;
};
[[nodiscard]] std::span<const BehaviorOperation> behavior_operations() noexcept;

}  // namespace omui::world
