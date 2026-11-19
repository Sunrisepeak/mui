# omui.lang.js

openmui for JavaScript and TypeScript, embedded with QuickJS-ng (component-binding §7), imported as a module: [`mcpplibs.mwrap.quickjs_ng`](https://github.com/mcpplibs/quickjs-ng-m) over `compat.quickjs-ng`, so no C header appears here. The script runs on [omui.lang](..)'s script thread. What it builds is a view document, so a view in TypeScript is the C++ builder's tree (CTS S level: `tests/js_test.cpp`, and example 03).

```ts
import { signal, fmt, column, text, button, mount } from "openmui";
const count = signal(0);
export const Counter = () => column({ gap: 8 }, [
  text({ text: fmt("{} clicks", count) }),
  button({ id: "inc", text: "+1", onActivated: () => count.update((v) => v + 1) }),
]);
mount("main", Counter());
```

The ES module `openmui` provides:

- **Builders**: one for every registered component, in camel case (`textBox`, `scrollView`). They are written `type(props, children)` or `type(children)`. Props are camel case (`maxWidth` is `max_width`), and handlers are `on<Event>`. A signal given to a property binds two-way where the property is bindable, and one-way elsewhere. `.d.ts` types are generated from the contracts (`spec/stubs/core.d.ts`).
- **Signals and expressions**:
  - signals: `signal(v [, key])` with `get`, `set`, `update`, `watch` and `value`. They are mirrored, so a read never waits;
  - expressions: `fmt`, `call(fn, …)`, `select(c, a, b)`, `eq` / `ne` / `lt` / `le` / `gt` / `ge`, `and` / `or` / `not` and `add` / `sub` / `mul` / `div` / `mod` / `neg`, plus the same as methods (`count.add(1)`). The runtime evaluates them synchronously; no JavaScript runs in a frame.
- **Regions**: `when(cond, then, else)`, `each(model, (row) => element [, where])`, where `where` is an expression or a function of the row (`(row) => row.done.not()`) and `row.title` is an expression and `slot(name)`.
- **The rest of the profile**:
  - views: `mount`, `unmount` and `document`;
  - verbs: `verb(name, {effect, answer, summary}, fn)` (S-1, S-2) and `invoke`; `task(name, {summary, steps})`, a task to begin with (spec §5, D-186): steps of verbs or nodes' actions (`dryRun` for one that would ask an answer), refused with the runtime's reason, the same again on a reload a no-op;
  - `model(name)`, with `push`, `splice`, `rows` and `clear`;
  - `find(path or query)` with `get` and `set`, plus `act`, `snapshot`, `tr` and `log`.
- **Errors are exceptions** (`OmuiError`), with the message the runtime gave (`did you mean "button"?`). An exception in a handler, a verb or an unhandled promise goes to `Script::on_error`. Promise jobs run after every call, so async handlers work.
- **Modules**: a script imports `"openmui"`, `"openmui:native/<name>"` and its own modules by path (`"./label"`: the file, or with `.ts`, `.mts`, `.js`, `.mjs`, or the directory's `index`), loaded as they are imported. Any other import is an error that says so.
- **Hot reload**: `Script::reload(code)` evaluates the module again under a new name. The n-th `signal()` returns the same signal with its value, views are reconciled, and handlers are replaced. The tests require it to finish in under 200 ms.

For the player: `javascript()` (`*.js`, `*.mjs`) and `typescript()` (`*.ts`, `*.mts`).

**On the Web** (D-237) the page's own engine runs a script — no second engine in the program: `src/js/omui_script.js`, linked as an Emscripten library (`build.mcpp`), evaluates each module by the page's `import()` (its imports linked to the script's own `openmui` and `openmui:native/*`), and the program answers the same natives (`Natives`, the table QuickJS uses) as text. A run's future settles when the page's import does; the player collects it at a frame boundary, never waiting. A program that does wait (a test, a tool) calls `let_page_run()` between its looks, which lets the page run where the program is built to suspend (`-sASYNCIFY`); `reload_now` (Live Design's edits) waits so, about a millisecond a reload in headless Chromium, and is refused, with that reason, in a program that cannot suspend. A module told it changed is a module of its own on the page (the page knows a module by its text), so it is evaluated again as with QuickJS. `omui-devtools check browser` runs these cases in Chromium.

**TypeScript** (D-139) runs as the JavaScript left when its types are blanked, in this process and on the Web, with no tool: [mlibs.ts](../../../../base/ts/README.md).

The conformance scenarios (`spec/conformance/S`, CTS level S) run in JavaScript through `tests/cts.js`. It builds each view with the builders and the expression functions and does the steps through `act`, `invoke` and the rest. It must record the trace pinned beside each scenario.

**Native modules** (component-binding §8) are made from the program's catalogue, with no binding written by hand: `import { fuzzy } from "openmui:native/fuzzy"`: an `async` function answers a promise, and has a `…Sync` form for where a promise cannot be awaited (a verb).

**Themes** (omui.theme): a theme, then overlays — `theme("dark", "compact")` (an `OmuiError` when it cannot be composed) and `themes()`.
