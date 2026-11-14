// openmui.ui.canvas.kit:chart — a behaviour file as read (not exported):
// its JSON as a plain tree with positions (tree.cpp), and the statechart
// compiled from it.
module omui.ui.canvas.kit:chart;

import std;
import omui.core;
import omui.expr;
import omui.ui.canvas;
import :declarative;

namespace omui::kit {

struct Tree {
    enum class Type : std::uint8_t { table, array, text, integer, real, boolean, other };
    // Written out: GCC 16.1 deletes the implicit one of a type that holds a vector of itself.
    Tree() {}
    Type                     type = Type::table;
    int                      line = 0, column = 0;
    std::vector<std::string> keys;   // a table's, in order; items are its values
    std::vector<Tree>        items;  // a table's values, or an array's
    std::string              text;
    double                   number = 0;
    bool                     boolean = false;

    [[nodiscard]] const Tree* get(std::string_view key) const noexcept {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &items[i];
        return nullptr;
    }
};

// tree.cpp: the JSON as a tree, or where it is malformed.
[[nodiscard]] std::expected<Tree, world::BehaviorProblem> parse_tree(std::string_view text, std::string_view file);

enum class Event : std::uint8_t { pointer_down, pointer_move, pointer_up, pointer_cancel, tap, key };
inline constexpr std::array<std::string_view, 6> event_names{"pointer_down", "pointer_move", "pointer_up", "pointer_cancel", "tap", "key"};

enum class Op : std::uint8_t {
    capture, move, move_to, resize, raise, select, add_to_selection, toggle_selection, clear_selection,
    connect, disconnect, remove, commit, set, fit, preset, zoom_to,
};

// An operation, its arguments compiled.
struct Step {
    Op                      op = Op::capture;
    std::vector<expr::Expr> args;
    std::string             text;  // as written, for the log
    int                     line = 0;
};

struct Transition {
    std::optional<expr::Expr> when;
    int                       to = -1;  // -1: stays
    std::vector<Step>         steps;
};

struct State {
    std::string                                   name;
    std::vector<Step>                             entry, exit;
    std::array<std::vector<Transition>, 6>        on;
};

struct ActionDef {
    std::string                                       name, doc, answer;
    spec::Effect                                      effect = spec::Effect::reversible;
    std::vector<std::pair<std::string, std::string>> args;
    std::optional<expr::Expr>                         when;
    std::vector<Step>                                 steps;
};

struct Chart {
    std::string            name, file, doc;
    int                    priority = 0;
    int                    initial = 0;
    std::vector<State>     states;
    std::vector<ActionDef> actions;
};

}  // namespace omui::kit
