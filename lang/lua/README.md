# openmui.lang.lua

openmui for Lua 5.4, embedded (component-binding §7) and imported as a module: [`mcpplibs.mwrap.lua`](https://github.com/mcpplibs/lua-m) over `compat.lua`, so no C header appears here. The script runs on [openmui.lang](../common)'s script thread. What it builds is a view document, so a view in Lua is the C++ builder's tree (CTS S level; `tests/lua_test.cpp`).

```lua
local count = omui.signal(0)
local function Counter()
  return omui.Column { gap = 8,
    omui.Text   { text = omui.fmt("{} clicks", count) },
    omui.Button { id = "inc", text = "+1", on_activated = function() count:update(function(v) return v + 1 end) end },
  }
end
omui.mount("main", Counter())
```

The module is the global `omui`, and also `require "openmui"`:

- **Builders**: `omui.<Component> { props…, children…, on_<event> = fn, animate = {…} }` for any registered component, by PascalCase or by contract name. A table's children are its array part. A signal given to a property binds two-way where the property is bindable (from the runtime's catalogue) and one-way elsewhere. LuaLS annotations are generated from the contracts (`spec/stubs/core.lua`).
- **Signals**: `omui.signal(v [, key])` with `:get()`, `:set(v)`, `:update(fn)` and `:watch(fn)`. They are runtime signals mirrored here, so `:get()` never waits.
- **Expressions**: signal arithmetic (`a * b + 1`, `-a % 4`), and `omui.fmt`, `select`, `eq` / `ne` / `lt` / `le` / `gt` / `ge`, `and_` / `or_` / `not_`, and `call(fn, …)` for any function of the expression language. The runtime evaluates them synchronously; no Lua runs in a frame (§6.3).
- **Regions**: `omui.when(cond, {…}, {…})`, `omui.each(model, function(row) … end [, where])` and `omui.slot(name)`. The filter `where` is an expression, or a function of the row (`function(row) return omui.not_(row.done) end`). A loop's body, and a filter that is a function, are built once with `row` as an expression (`row.title`). Its handlers receive the row as `event.rows.row`.
- **Calls into the runtime**:
  - views: `omui.mount` and `unmount` (`omui.document(e)` shows the document);
  - verbs: `omui.verb(name, {effect, answer, summary}, fn)`, which is implemented in Lua under S-1 and S-2, and `omui.invoke`;
  - models: `omui.model(name)` with `:push`, `:splice` and `:rows`;
  - the semantic tree: `omui.find(path or query)` with `:get` and `:set`, plus `omui.act` and `omui.snapshot`;
  - also `omui.tr`, `omui.null` and `omui.log`.
- **Errors are values**: a call that fails answers `nil, message`. Calling with the wrong arguments raises a Lua error. Errors raised in handlers and verbs go to `Script::on_error`.
- **Hot reload**: `Script::reload(code)` runs the chunk again in place. The n-th `omui.signal` returns the same signal, or the one with the same key, with its value. Views are reconciled and handlers replaced. The tests require it to finish in under 200 ms (plan §19, M3 ④).

Lua errors unwind with `longjmp`, so no entry point raises while a C++ object is alive, and every call back into Lua is protected.

The conformance scenarios (`spec/conformance/S`, CTS level S) run in Lua through `tests/cts.lua`. It builds each view with the Lua builders and metamethods and does the steps through `omui.act`, `omui.invoke` and the rest. It must record the trace pinned beside each scenario, which Python and JavaScript record too.

**Native modules** (component-binding §8) are made from the program's catalogue, with no binding written by hand: `omui.native.fuzzy.rank(q, files, 20)`: positional arguments in the contract's order; it answers the result, or `nil, why`. An `async` function is called synchronously (Lua has no promises).

**Themes** (openmui.theme): a theme, then overlays — `omui.theme("dark", "compact")` (true, or nil and why) and `omui.themes()`.
