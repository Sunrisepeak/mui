// openmui.semantic:query — what to look for in the tree, and what to wait for
// (`find` and `wait` of openmui.tree@0.1, spec §9).
//
// A query matches nodes by id or subtree, role, name (a glob: `删除*`),
// states present or absent, value, and place (inside a rectangle, under a
// point); a condition waits for a match to appear or to go, and for the
// interface to settle (no animation running, nothing being removed).
export module openmui.semantic:query;

import std;
import openmui.core;

export namespace omui::semantic {

struct Query {
    std::string                id;           // exactly this node
    std::string                under;        // in this subtree (the node itself excluded)
    std::optional<Role>        role;
    std::string                name;         // a glob: * any run, ? one character
    std::vector<State>         states;       // all of these
    std::vector<State>         without;      // none of these
    std::optional<Value>       value;        // equal to this
    std::optional<Rect>        within;       // bounds intersect this rectangle
    std::optional<Point>       at;           // bounds contain this point
    bool                       interactive = false;  // offers an action other than read, or takes focus
    std::size_t                limit = 0;    // 0: all

    [[nodiscard]] bool matches(const spec::SemanticNode& node) const;

    // {"id", "under", "role", "name", "state": [...], "not": [...], "value",
    //  "within": [x, y, w, h], "at": [x, y], "interactive", "limit"}
    [[nodiscard]] static std::expected<Query, std::string> parse(const Value& v);
    [[nodiscard]] Value to_value() const;
};

struct Condition {
    std::optional<Query> query;
    bool                 absent = false;   // wait for no node to match
    bool                 settled = false;  // and for no node to be animating or removing

    // {"q": {...}, "absent": bool, "settled": bool}
    [[nodiscard]] static std::expected<Condition, std::string> parse(const Value& v);
};

// `*` matches any run of characters, `?` one (UTF-8) character.
[[nodiscard]] bool glob(std::string_view pattern, std::string_view text) noexcept;

}  // namespace omui::semantic
