// omui.semantic:query — what to look for in the tree, and what to wait for
// (`find` and `wait` of openmui.tree@0.1, spec §9).
//
// A query matches nodes by id or subtree, role, name (a glob: `删除*`),
// states present or absent, value, and place (inside a rectangle, under a
// point); a condition waits for a match to appear or to go, and for the
// interface to settle (no animation running, nothing being removed).
export module omui.semantic:query;

import std;
import mlibs.error;
import omui.core;

export namespace omui::semantic {

// Why a query or a condition cannot be read (mlibs.error): its code, the field where, and what was found.
enum class QueryError : std::uint8_t { not_an_object, unknown_field, unknown_role, unknown_state, not_a_list, bad_rect, bad_point, nothing_to_wait_for };
[[nodiscard]] constexpr mlibs::error::Description describe(QueryError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case QueryError::not_an_object: return {"omui.semantic.not-an-object", Kind::invalid_argument, "a query or a condition is an object", "write the query or condition as a JSON object: {\"role\": \"button\", \"name\": \"Save\"}"};
        case QueryError::unknown_field: return {"omui.semantic.unknown-field", Kind::invalid_argument, "a field a query or a condition does not have", "use the fields a query has (id, under, role, name, state, not, value, within, at, interactive, limit) or a condition has (q, absent, settled, timeout)"};
        case QueryError::unknown_role: return {"omui.semantic.unknown-role", Kind::invalid_argument, "no role of that name", "use a role the tree has (`ui.snapshot` shows each node's role)"};
        case QueryError::unknown_state: return {"omui.semantic.unknown-state", Kind::invalid_argument, "no state of that name", "use a state the tree has (`ui.snapshot` shows each node's states)"};
        case QueryError::not_a_list: return {"omui.semantic.not-a-list", Kind::invalid_argument, "a list of state names", "give the states as a list of names: [\"checked\"]"};
        case QueryError::bad_rect: return {"omui.semantic.bad-rect", Kind::invalid_argument, "[x, y, width, height]", "give a rectangle as four numbers: [x, y, width, height]"};
        case QueryError::bad_point: return {"omui.semantic.bad-point", Kind::invalid_argument, "[x, y]", "give a point as two numbers: [x, y]"};
        case QueryError::nothing_to_wait_for: return {"omui.semantic.nothing-to-wait-for", Kind::invalid_argument, "a condition needs `q` or `settled`", "give the condition a query (`q`) or `settled: true`"};
    }
    return {"omui.semantic.?", Kind::internal};
}
using QueryFailure = mlibs::error::Error<QueryError>;

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

    // {"id", "under", "role", "name", "state": [...], "not": [...], "value" (a number: a range's value now too),
    //  "within": [x, y, w, h], "at": [x, y], "interactive", "limit"}
    [[nodiscard]] static std::expected<Query, QueryFailure> parse(const Value& v);
    [[nodiscard]] Value to_value() const;
};

struct Condition {
    std::optional<Query> query;
    bool                 absent = false;   // wait for no node to match
    bool                 settled = false;  // and for no node to be animating or removing

    // {"q": {...}, "absent": bool, "settled": bool}
    [[nodiscard]] static std::expected<Condition, QueryFailure> parse(const Value& v);
};

// `*` matches any run of characters, `?` one (UTF-8) character.
[[nodiscard]] bool glob(std::string_view pattern, std::string_view text) noexcept;

}  // namespace omui::semantic
