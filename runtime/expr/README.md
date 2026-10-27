# openmui.expr

The expression language of bindings (`view-data-files` §4,
`spec/registry/expr.toml`): one AST, written two ways — combinators in code,
CEL-compatible text in view documents and behaviours — and evaluated
synchronously by native code.

```cpp
using namespace omui::expr;
auto e = parse("count > 0 ? format('{} left', count) : 'done'");
auto r = evaluate(*e, Value::Map{{"count", Value(3)}});   // r.value == "3 left"

Expr same = select(ref("count") > 0, fmt("{} left", ref("count")), "done");
same.same(*e);   // true
same.text();     // count > 0 ? format("{} left", count) : "done"
```

- **Text**: CEL's literals (ints, hex, `u` suffix, doubles, strings in four
  quotings with every escape, raw strings, lists, maps), names, member
  access, indexing, `has(a.b)`, calls in both forms (`size(x)`, `x.size()`),
  `- ! * / % + - < <= > >= == != in && || ?:` with CEL's precedence. A
  conditional is also accepted in the middle branch (`a ? b ? c : d : e`).
- **Printing**: `text()` is CEL that parses back to the same tree — the round
  trip is fuzzed with random well-formed and random malformed input.
- **Diagnostics**: syntax errors carry line, column and a fix
  (`count = 3` → "compare with `==`"; `a and b` → "CEL writes && for and").
  Evaluation never throws: an error makes the value null and gives a
  diagnostic at the failing node (`locate()` adds line and column), with a
  hint where one is known (`user.email` → "test it first: has(….email)";
  `sise(x)` → "did you mean `size`?").
- **Semantics**: CEL's, with one deliberate difference for interfaces —
  arithmetic mixing int and double is done in double. ints are 64-bit and
  their overflow, division and modulus by zero are errors; equality and
  ordering work across int and double; `&&` and `||` absorb an error on
  either side the way CEL does (`missing && false` is false).
- **Functions**: exactly the registry's (`functions()` is generated from it):
  `format`, `size`/`len` (grapheme clusters), `contains`, `startsWith`,
  `endsWith`, `lower`/`upper` (Unicode full case mapping), `trim`, `min`,
  `max`, `clamp`, `abs`, `round`/`floor`/`ceil`, `int`/`double`/`string`,
  `lookup`, `tr` (the context's catalogue), and for motion `interpolate`,
  `ease` and `spring` — the same curves and closed-form springs as
  `openmui.anim`, so a binding and an animation agree to the bit.
- **Bounded**: no tree is taller than `max_depth` (200) — parentheses,
  prefix operators and long chains alike — so parsing, evaluating, printing
  and comparing never overflow the stack; trees built in code are checked
  before they are evaluated.

Depends on `openmui.spec`, `openmui.unicode` and `openmui.anim`.
