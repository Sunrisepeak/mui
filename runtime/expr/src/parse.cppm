// openmui.expr:parse — CEL-compatible text to the AST.
//
// The subset of registry/expr.toml: literals (null, booleans, ints, doubles,
// strings with escapes, lists, maps), names, member access, indexing,
// function calls in both forms (`size(x)`, `x.size()`), `has(a.b)`, the
// arithmetic, comparison and logical operators, `in`, and `c ? a : b`.
// Errors carry where and what, and a suggestion; nesting deeper than
// max_depth is an error rather than a stack overflow (the parser is fuzzed).
export module openmui.expr:parse;

import std;
import openmui.spec;
import :ast;

export namespace omui::expr {

struct Diagnostic {
    std::uint32_t at = 0;            // byte offset in the text the node was parsed from
    std::uint32_t line = 0, column = 0;  // 0: not known (an expression built in code)
    std::string   message;
    std::string   hint;              // how to fix it, when one is known
    [[nodiscard]] std::string text() const;  // "1:7: message (hint)"
};

// Fills in the line and column of an evaluation's diagnostic from the text
// the expression was parsed from.
void locate(Diagnostic& d, std::string_view source) noexcept;

inline constexpr int max_depth = 200;

[[nodiscard]] std::expected<Expr, Diagnostic> parse(std::string_view text);

}  // namespace omui::expr
