// openmui.expr:eval — evaluating an expression, synchronously.
//
// Names resolve through the environment (a binding's signals, a component's
// properties, a model's row). Evaluation has no side effects and always ends;
// a type error, a missing name, an overflow or a division by zero gives the
// expression the value null and a diagnostic at the node that failed, never
// an exception — the binding then shows its property's default (spec: an
// error yields the type's default value and a diagnostic).
//
// Numbers follow CEL, with one deliberate difference for user interfaces:
// arithmetic that mixes an int and a double is done in double (CEL refuses
// it). ints are 64-bit and their overflow is an error; comparisons and
// equality work across int and double.
export module openmui.expr:eval;

import std;
import openmui.spec;
import :ast;
import :parse;

export namespace omui::expr {

// What a name means: its value, or nothing when it is not defined.
using Resolve = std::function<std::optional<Value>(std::string_view name)>;

struct Context {
    // tr(key, {args}): the message of the key in the person's language,
    // formatted with its named arguments (MessageFormat 2). Without one, the
    // key is the message: tr('{$n} left', {'n': n}) reads well untranslated.
    std::function<std::string(std::string_view key, const Value& args)> translate;
};

struct Result {
    Value                   value;
    std::vector<Diagnostic> errors;  // empty: it evaluated
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

[[nodiscard]] Result evaluate(const Expr& e, const Resolve& names, const Context& context = {});
// A map of names, for tests and constant folding.
[[nodiscard]] Result evaluate(const Expr& e, const Value::Map& names, const Context& context = {});

// The names of the registry's functions (registry/expr.toml), sorted.
[[nodiscard]] std::span<const std::string_view> functions() noexcept;

// The text of a value as format() and string() write it: shortest round-trip
// doubles, JSON for lists and maps.
[[nodiscard]] std::string text_of(const Value& v);

}  // namespace omui::expr
