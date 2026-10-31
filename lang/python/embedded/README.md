# openmui.lang.python.embedded

openmui for Python embedded in a C++ program (component-binding §7): [PocketPy](https://github.com/pocketpy/pocketpy) 2.2 through [`mcpplibs.mwrap.pocketpy`](https://github.com/mcpplibs/pocketpy-m) over `compat.pocketpy`, so no C header appears here. The script runs on [openmui.lang](../..)'s script thread, as Lua ([lang/lua](../../lua)) and JavaScript ([lang/js](../../js)) do. Host mode ([lang/python](..), CPython loading openmui as an extension) is the other way round; the `openmui` module is the same in both, so a script reads the same: example 03's `python/tally.py` runs unchanged in either, with the tree of the C++ version (`examples/03-counter/tests`).

```python
import openmui as omui

count = omui.signal(0)

def counter():
    with omui.column(gap=8):
        omui.text(text=omui.fmt("{} clicks", count))
        omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))

omui.mount("main", counter)
```

```cpp
omui::lang::Host host(runtime);
omui::lang::Session session(host);
omui::lang::python::embedded::Script script(session);
auto ran = script.run(code, "counter.py");    // a future of Result<void>
```

The player runs `*.py` (`lang = "py"`; `language()`), and reloads a saved file in place.

## The API

- **Builders.** `omui.<component>(*children, id=…, props…, on_<event>=fn, animate={…})` for every registered component, by its contract name (`omui.text_box`), as calls or as `with` blocks; `omui.element(name, …)` for any name. A signal given to a property binds two-way where the property is bindable (from the runtime's catalogue) and one-way elsewhere.
- **Signals.** `omui.signal(v, key=None)` has `.get()`, `.set(v)`, `.update(fn)`, `.watch(fn)` and `.value`. They are runtime signals mirrored here, so `.get()` never waits.
- **Expressions.** Python's operators on signals and expressions (`a * b + 1`, `-a % 4`, `count >= goal`, `a & b`, `~a`, `row.title`), and `omui.fmt`, `call(fn, …)`, `select(cond, then, otherwise)`, `eq` / `ne` / `lt` / `le` / `gt` / `ge`, `and_` / `or_` / `not_`. The runtime evaluates them; no Python runs in a frame (§6.3). An expression has no truth value (`if count == 0` raises TypeError).
- **Regions.** `omui.when(cond, then, otherwise)`, `omui.each(model, lambda row: …, where=…)` and `omui.slot(name)`. A loop's body, and a `where` that is a function, are built once with `row` as an expression. Its handlers receive the row as `event["rows"]["row"]`.
- **Calls into the runtime:**
  - views: `omui.mount(slot, view)` (an element, or a function that builds one), `mount_document`, `unmount`, and `omui.document(view)`, the view document a view is;
  - verbs: `omui.verb(name, fn, effect=…, answer=…, summary=…, person=False)`, or `@omui.verb(name, …)`, implemented in Python under S-1 and S-2; `omui.invoke(name, args)`;
  - models: `omui.model(name)` with `.push`, `.splice` (from 0), `.rows` and `.clear`;
  - the semantic tree: `omui.find(path or query)` with `.get(prop)` and `.set(prop=value)`, `omui.act(node=…, action=…)` and `omui.snapshot(root=…)`;
  - `omui.tr(key, name=value)`, `omui.theme("dark", "compact")` and `omui.themes()`, `omui.log(…)`;
  - native modules (component-binding §8), made from the program's catalogue: `omui.native.fuzzy.rank(q, files, 20)`, or `from openmui.native import fuzzy`; by position or by name. An `async` function is waited for, as in Lua.
  - `omui.run(view)` mounts the view at `main` and answers 0, so a script written for host mode (`raise SystemExit(omui.run(tally))`) runs here; the program owns the window and the frames.
- **Errors are exceptions.** A refused call raises `omui.OmuiError` with the runtime's message (``no component is named `buton` (did you mean `button`?)``). A run that raises fails with the traceback, which names the file and the line. An exception in a handler, a watch or a verb goes to `Script::on_error`, and a verb that raises is refused with its last line.
- **Hot reload.** `Script::reload(code)` runs the script again in `__main__`. The n-th `omui.signal` returns the same signal, or the one with the same key, with its value. Views are reconciled; handlers, watches and verbs are replaced (a verb keeps its effect and answer). Globals the script does not assign again stay. The tests require it to finish in under 200 ms (plan §19, M3 ④); it takes about a millisecond.
- **The host stays in charge.** `exit()` raises `SystemExit`, which ends the script, not the program: a clean end for `exit()` and `exit(0)`, an error for another code. A run, a handler or a verb that loops is stopped with `TimeoutError` after `Script::budget` of processor time (5 s by default; PocketPy's watchdog counts the process's time, so it is a bound, not a measure).

## How it is made

- **One interpreter, many scripts.** PocketPy has one current VM for the process and no threads. Each `Script` takes a VM of its own (sixteen at most) and enters the interpreter under one lock. A call that waits on the runtime (a mount, an invoke, a snapshot) gives the lock up meanwhile, so another script's thread can run, and a verb of one script called from another on the same thread finds its own VM. A Script's VM is emptied (`py_resetvm`) for the next when it goes.
- **The module** `openmui` is Python (`src/prelude.cppm`, host mode's `python/openmui` in PocketPy's subset) over a native module `_openmui` (`src/native/runtime.cpp`), which turns Python values into JSON and back and calls [openmui.lang](../..). The Python functions the runtime may call later are kept in a dict of `_openmui`, where PocketPy's collector sees them. A view's handlers are released when the view is replaced.
- **The conformance scenarios** (`spec/conformance/S`, CTS level S) run in embedded Python through `tests/cts.py`: it builds each view with the builders and operators and does the steps through `omui.act`, `omui.invoke` and the rest, and must record the trace pinned beside each scenario, as the Lua, JavaScript and host-mode Python runners do.

## Python, PocketPy's subset

PocketPy implements most of Python 3. What a script here cannot use, as of 2.2:

- generator expressions (`sum(x for x in y)`: write `sum([x for x in y])`), `finally`, keyword-only parameters (`def f(a, *, b)`), defaults that are not literals (`def f(a=())`), `raise … from …`, `str.split`'s `maxsplit`, a module's `__getattr__`;
- `match` is a keyword, not only a soft one: `fuzzy.match` cannot be written, so a native function of that name is also `fuzzy.match_` (or `getattr(fuzzy, "match")`);
- `dict(a)` of a dict is refused (`a.copy()` works), and `{**a, **b}` makes a set of wrappers rather than a dict, silently;
- `int` is 64 bits and wraps (`2 ** 64 == 0`), and `isinstance(True, int)` is false;
- `RuntimeError` cannot be subclassed (`OmuiError` is an `Exception`).

## Not done

- **The builders are made when the Script starts**, from the catalogue then (PocketPy has no module `__getattr__`): a component registered later is reached with `omui.element(name, …)`.
- **Host mode's frame control** (`omui.settle`, `step`, `png`, `pixels`, `reload`) is not here: the C++ program owns the frames and reloads through `Script::reload`.
- **No typing stubs**: the wheel's `.pyi` (host mode) types the same names, but nothing checks an embedded script before it runs.
- **No sandbox**: `os`, `open` and `import` from files are PocketPy's, as Lua's `os` and `io` are Lua's; a host that runs untrusted scripts restricts them (`py_callbacks()->importfile`) itself.
