# openmui.abi

`openmui.h` implemented (spec §8). It is the one boundary every language binding is written against: TypeScript, Python, Lua, and any language that can call C.

- **Only handles and values cross it.** No C++ type and no exception crosses (every function is `noexcept`). Every function returns `OmuiResult`. Text and structured results reach a sink the caller supplies, so no memory changes hands.
- **Views are view documents.**
  - `omuiMount` loads a `*.view.omui.json` text at a slot ([openmui.viewdoc](../viewdoc/README.md)). Mounting the same slot again reconciles in place and keeps the state.
  - `omuiCheck` returns the diagnostics as JSON lines.
  - `omuiDefineComponent` makes a document with an interface a component.
  - `omuiSignalFind` gives a binding's own signal object the document's signal.
- **Nodes.**
  - `omuiFind` takes an id path, or a query of the bus interface (`{"role": "button", "name": "Save*"}`). It reveals a virtualised list's row (S-7).
  - `omuiGet` reads a property; `omuiSet` applies a batch at the next frame boundary.
  - `omuiOn` / `omuiOff` subscribe to events, coalescing high-frequency events to one per frame.
  - `omuiBind` binds a property to a signal, two-way where the property is bindable.
- **Signals and verbs.**
  - `omuiSignalCreate`, `Set`, `Get` and `Watch` (called after each batch that changed the value).
  - `omuiRegisterVerb` registers a verb implemented in C. Its effect classifies the actions that invoke it, and an irreversible verb must state its answer (S-1).
  - `omuiInvoke` returns the envelope: `OMUI_NEEDS_ANSWER` when the answer is missing (S-2), and nothing waits.
- **The bus interface in-process.** `omuiSnapshot`, `omuiAct` and `omuiWait` are `snapshot`, `act` and `wait` of `openmui.tree@0.1`. `omuiWait` runs a headless runtime's frames until the condition holds or `OMUI_NOT_READY`.
- **Frames.**
  - `omuiFrame` drives a headless runtime. It takes the frame's time, the surface size and scale, and input events in the host interface's JSON. It answers the semantic revision, whether to animate again, and the pixels as premultiplied BGRA8 when a buffer is given.
  - `omuiPost` runs work on the runtime's thread at the next frame boundary, and may be called from any thread.
  - `omuiRun` shows a non-headless runtime in a window. The desktop host lends its loop through `omui::abi::set_window_runner`; without one the answer is `OMUI_ERROR_EXTENSION_NOT_PRESENT`.

Generated from `spec/registry/abi.toml` by `omui-gen abi`:
- the types (`src/generated/c.cppm`, the header's names in `omui::abi`);
- the C declarations (`src/generated/api.cppm`);
- `src/native/layout.cpp`, which includes the header and checks every enumerator, every struct's size and alignment, and every field's offset against the C++ side.

The tests cover the C ABI from both languages:
- `tests/abi_test.cpp` covers every function, including each misuse, which is answered by a result and never a crash;
- `tests/c_abi_test.c` is a C program that includes `openmui.h`, links the library, mounts a document, acts on a node and reads the result.

Observability: the log category `abi`, and the trace span `abi.frame`.
