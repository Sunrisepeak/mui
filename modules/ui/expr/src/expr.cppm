// openmui.expr — the expression language of bindings (view-data-files §4).
//
// One AST (registry/expr.toml), two ways to write it: combinators in code and
// CEL-compatible text in view documents and behaviours.
//
//     using namespace omui::expr;
//     auto e = parse("count > 0 ? format('{} left', count) : 'done'");
//     auto r = evaluate(*e, Value::Map{{"count", Value(3)}});   // r.value == "3 left"
//
//     Expr same = select(ref("count") > 0, fmt("{} left", ref("count")), "done");
//     same.text();   // the CEL text again
export module openmui.expr;

export import openmui.spec;
export import :ast;
export import :parse;
export import :eval;
