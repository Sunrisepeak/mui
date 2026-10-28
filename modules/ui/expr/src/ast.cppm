// openmui.expr:ast — the expression AST and the combinators that build it.
//
// An Expr is an immutable tree of nodes; combinators share their arguments'
// subtrees, so building is cheap and an expression can be reused anywhere.
// The operators and functions are registry/expr.toml's; their C++ spellings
// are the registry's `combinators.cpp` names (`not_`, `and_`, `fmt` …).
//
//     using namespace omui::expr;
//     Expr e = fmt("{} of {} done", ref("done"), size(ref("tasks")));
//     Expr f = select(ref("count") > 0, "some", "none");
export module openmui.expr:ast;

import std;
import openmui.spec;

export namespace omui::expr {

using spec::Value;

enum class Op : std::uint8_t {
    literal,   // value
    ref,       // name: a value of the environment (a signal, a property)
    member,    // args[0].name
    index,     // args[0][args[1]]
    neg, not_, mul, div, mod, add, sub, lt, le, gt, ge, eq, ne, in, and_, or_,
    select,    // args[0] ? args[1] : args[2]
    call,      // name(args…)
    list,      // [args…]
    map,       // {args[0]: args[1], …}
    has,       // has(args[0].name): whether the field is there
};

[[nodiscard]] std::string_view name(Op op) noexcept;

struct Node {
    Op                                       op = Op::literal;
    Value                                    value;  // literal
    std::string                              name;   // ref, member, call, has
    std::vector<std::shared_ptr<const Node>> args;
    std::uint32_t                            at = 0;  // byte offset in the text it was parsed from
    std::uint32_t                            height = 1;  // of the tree: 1 for a leaf
};

class Expr {
public:
    Expr() : Expr(Value{}) {}
    Expr(Value v);  // a literal
    Expr(bool v) : Expr(Value(v)) {}
    Expr(int v) : Expr(Value(v)) {}
    Expr(std::int64_t v) : Expr(Value(v)) {}
    Expr(double v) : Expr(Value(v)) {}
    Expr(const char* v) : Expr(Value(v)) {}
    Expr(std::string v) : Expr(Value(std::move(v))) {}
    Expr(std::string_view v) : Expr(Value(v)) {}
    explicit Expr(std::shared_ptr<const Node> n) : node_(std::move(n)) {}

    [[nodiscard]] const Node& node() const noexcept { return *node_; }
    [[nodiscard]] const std::shared_ptr<const Node>& ptr() const noexcept { return node_; }

    // The CEL text that parses back to this expression.
    [[nodiscard]] std::string text() const;
    // The names it reads from its environment, sorted, each once.
    [[nodiscard]] std::vector<std::string> references() const;
    // The same tree (literals, names, shapes; not positions).
    [[nodiscard]] bool same(const Expr& other) const noexcept;

    [[nodiscard]] Expr operator[](Expr key) const;
    [[nodiscard]] Expr field(std::string name) const;

private:
    std::shared_ptr<const Node> node_;
};

[[nodiscard]] Expr make(Op op, std::vector<Expr> args, std::string name = {});

// ── combinators (registry/expr.toml) ──

[[nodiscard]] Expr ref(std::string name);
[[nodiscard]] Expr lit(Value v);
[[nodiscard]] Expr neg(Expr a);
[[nodiscard]] Expr not_(Expr a);
[[nodiscard]] Expr mul(Expr a, Expr b);
[[nodiscard]] Expr div(Expr a, Expr b);
[[nodiscard]] Expr mod(Expr a, Expr b);
[[nodiscard]] Expr add(Expr a, Expr b);
[[nodiscard]] Expr sub(Expr a, Expr b);
[[nodiscard]] Expr lt(Expr a, Expr b);
[[nodiscard]] Expr le(Expr a, Expr b);
[[nodiscard]] Expr gt(Expr a, Expr b);
[[nodiscard]] Expr ge(Expr a, Expr b);
[[nodiscard]] Expr eq(Expr a, Expr b);
[[nodiscard]] Expr ne(Expr a, Expr b);
[[nodiscard]] Expr in(Expr a, Expr b);
[[nodiscard]] Expr and_(Expr a, Expr b);
[[nodiscard]] Expr or_(Expr a, Expr b);
[[nodiscard]] Expr select(Expr cond, Expr then, Expr otherwise);
[[nodiscard]] Expr has(Expr object, std::string field);
[[nodiscard]] Expr list(std::vector<Expr> items);
[[nodiscard]] Expr map(std::vector<std::pair<Expr, Expr>> entries);
// A function of the registry by name: call("clamp", {x, 0, 1}).
[[nodiscard]] Expr call(std::string function, std::vector<Expr> args);

template <class... A>
[[nodiscard]] Expr fmt(Expr pattern, A&&... args) {
    return call("format", {std::move(pattern), Expr(std::forward<A>(args))...});
}
// A message of the person's language (MessageFormat 2), its arguments by
// name: tr("inbox.count", {{"count", ref("count")}}) is tr('inbox.count', {'count': count}).
[[nodiscard]] inline Expr tr(Expr key, std::vector<std::pair<std::string, Expr>> args = {}) {
    if (args.empty()) return call("tr", {std::move(key)});
    std::vector<std::pair<Expr, Expr>> entries;
    for (auto& [k, v] : args) entries.emplace_back(Expr(k), std::move(v));
    return call("tr", {std::move(key), map(std::move(entries))});
}
[[nodiscard]] inline Expr lookup(Expr container, Expr key) { return call("lookup", {std::move(container), std::move(key)}); }
[[nodiscard]] inline Expr lookup(Expr container, Expr key, Expr fallback) {
    return call("lookup", {std::move(container), std::move(key), std::move(fallback)});
}
[[nodiscard]] inline Expr size(Expr x) { return call("size", {std::move(x)}); }
[[nodiscard]] inline Expr interpolate(Expr x, Expr input, Expr output) { return call("interpolate", {std::move(x), std::move(input), std::move(output)}); }
[[nodiscard]] inline Expr interpolate(Expr x, Expr input, Expr output, Expr ease) {
    return call("interpolate", {std::move(x), std::move(input), std::move(output), std::move(ease)});
}
[[nodiscard]] inline Expr spring(Expr t, Expr from, Expr to) { return call("spring", {std::move(t), std::move(from), std::move(to)}); }

// Operators, for C++ that reads like the text.
[[nodiscard]] inline Expr operator-(Expr a) { return neg(std::move(a)); }
[[nodiscard]] inline Expr operator!(Expr a) { return not_(std::move(a)); }
[[nodiscard]] inline Expr operator*(Expr a, Expr b) { return mul(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator/(Expr a, Expr b) { return div(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator%(Expr a, Expr b) { return mod(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator+(Expr a, Expr b) { return add(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator-(Expr a, Expr b) { return sub(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator<(Expr a, Expr b) { return lt(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator<=(Expr a, Expr b) { return le(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator>(Expr a, Expr b) { return gt(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator>=(Expr a, Expr b) { return ge(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator&&(Expr a, Expr b) { return and_(std::move(a), std::move(b)); }
[[nodiscard]] inline Expr operator||(Expr a, Expr b) { return or_(std::move(a), std::move(b)); }

}  // namespace omui::expr
