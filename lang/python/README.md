# openmui for Python

`openmui.lang.python` is openmui in Python, in host mode (component-binding §7, D-46): `import openmui` loads the runtime, the official components and the desktop host, and the program builds its views on them. A view in Python is the C++ builder's tree: example 03 in Python (`examples/03-counter/python/tally.py`) has the same semantic tree and the same pixels as the C++, view-document, Lua and TypeScript versions (CTS S level; `tests/test_example_counter.py`).

```python
import openmui as omui

count = omui.signal(0)

def counter() -> None:
    with omui.column(gap=8):
        omui.text(text=omui.fmt("{} clicks", count))
        omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))

omui.run(counter, title="Counter")    # --headless --png out.png runs it without a window
```

```sh
pip install ./lang/python       # builds the extension with mcpp: one cp310-abi3 wheel per platform
python examples/03-counter/python/tally.py
```

## The API

- **Builders.** `omui.<component>(*children, id=…, props…, on_<event>=fn, animate={…})` works for every registered component. Builders nest as `with` blocks or take their children as arguments. A signal given to a property binds two-way where the property is bindable (from the runtime's catalogue) and one-way elsewhere. The wheel's typing stubs (`__init__.pyi`, `py.typed`) are made from the same catalogue, with each property's type, each event and each component's documentation. `mypy --strict` checks the example.
- **Signals.** `omui.signal(v, key=None)` has `.get()`, `.set(v)`, `.update(fn)`, `.watch(fn)` and `.value`. Signals are runtime signals mirrored in Python, so `.get()` never waits.
- **Expressions.** Python operators on signals give expressions the runtime evaluates: `count + 1`, `count >= goal`, `a & b`, `~a`. The helpers are `omui.fmt`, `select(cond, then, otherwise)`, `eq` / `ne` / `lt` / `le` / `gt` / `ge`, `and_` / `or_` / `not_` and `call(fn, …)` for any function of the expression language. An expression has no truth value in Python, so `if count == 0` raises: no Python runs in a frame (§6.3).
- **Regions.** `omui.when(cond, then, otherwise)`, `omui.each(model, lambda row: …, where=lambda row: ~row.done)` and `omui.slot(name)`. A loop's body, and its filter when that is a function, are built once with `row` as an expression (`row.title`). Its handlers receive the row as `event["rows"]["row"]`.
- **Calls into the runtime:**
  - views: `omui.mount`, `mount_document` and `unmount`; `omui.document(view)` gives the view document a view is;
  - verbs: `@omui.verb(name, effect=…, answer=…, summary=…)` makes a verb implemented in Python, under S-1 and S-2; `omui.invoke` calls one;
  - models: `omui.model(name)` with `.push`, `.splice`, `.rows` and `.clear`;
  - the semantic tree: `omui.find(path or query)` with `.get` and `.set`, plus `omui.act` and `omui.snapshot`;
  - also `omui.tr` and `omui.log`.
- **Running.** `omui.run(view, title=…, size=…)` opens a window, or runs headless with `--headless`, `--png FILE`, `--frames N` or `OMUI_HEADLESS=1`. Tests use `omui.settle()`, `step(n)`, `png(file)` and `pixels()`. `pixels()` is the frame's hash, as the examples' tests pin it.
- **Errors are exceptions.** A refused call raises `omui.OmuiError` with the runtime's message, such as ``no component is named `buton` (did you mean `button`?)``. An exception in a handler or a verb prints its traceback; the runtime goes on.
- **Hot reload.** `omui.reload()` runs the script again. The n-th `omui.signal` returns the same signal, or the one with the same key, with its value.

## How it is made

- **The extension** `_openmui` is `src/entry/python`: a shared library holding the runtime and the host, with its own C++ runtime. It is written against CPython's stable ABI, the limited API of 3.10, without CPython's headers and without linking libpython. Its functions are resolved in the interpreter that loads it (`src/capi.cppm`, through `mcpplibs.os.library`), so one build serves every CPython 3.10+ of a platform.
- **Threads.** Python runs on [openmui.lang](../common)'s script thread. Every call releases the GIL and waits for the script thread. Every call from the runtime (a handler, a verb, a watch) takes the GIL there.
- **The Python half** is `python/openmui`: builders, expressions and `run`.
- **The wheel** comes from `packaging/openmui_build.py`, a PEP 517 backend in the tree. It builds the extension with mcpp and takes it from mcpp's result record. It bundles the shared libraries beside it and the third parties' notices, and generates the stubs. Its archives are reproducible (`SOURCE_DATE_EPOCH`). Configuration:
  - `--config-settings profile=dev|release` (default `release`);
  - `mcpp=PATH`, or `$OMUI_MCPP`, to choose the mcpp binary.
- **Tests.** `mcpp test -p openmui.entry.python` runs `tests/` in the system's CPython against the built extension. `tests/cts.py` is Python's runner of the conformance scenarios (`spec/conformance/S`): it must record each scenario's pinned trace, as the Lua and JavaScript runners do. CI also installs the wheel and runs example 03 headless.

**Native modules** (component-binding §8) are made from the program's catalogue, with no binding written by hand: `from openmui.native import fuzzy`, then `fuzzy.rank(q, files, 20)` or by keyword; a refusal is `OmuiError`. The wheel ships the official modules' `.pyi` stubs.
