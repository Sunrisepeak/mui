# openmui.lang

The Script Binding Profile (component-binding §6) over `openmui.h`, shared by every language binding: Lua ([lang/lua](../lua)), TypeScript and Python. Nothing here knows a language; a binding maps its values onto it.

- **Two threads** (`Host`, §6.2). The runtime's thread holds the tree. The script runs on its own thread. `ui(fn)` runs work on the runtime's thread and waits, at the next frame boundary; `send` does not wait; `post` queues work for the script. When the runtime calls into the script (a verb the script implements), it keeps doing what the script asks of it meanwhile. When the script waits on the runtime, it runs what the runtime waits for. So a script that invokes its own verb does not deadlock. `idle()` is what frame loops and tests wait for.
- **Views are documents** (`Session::mount`). An element tree becomes a view document (`document()` shows it). Constants stay constants; expressions (`fmt`, `select`, `call`, `binary`, …) become CEL that the runtime evaluates synchronously (§6.3, rule 3). A signal becomes a provided signal of the document, so every view and the script share one state. A handler becomes the document's own event, forwarded to the script with the event and the rows of the loops it sits in. A view written in any language is therefore the C++ builder's tree (CTS S level; `tests/lang_test.cpp` compares it with the document written by hand).
- **Signals are mirrored.** `signal`, `get`, `set` and `watch`. Reading never waits. A write is sent and not waited for. Changes from either side reach the script's watchers.
- **The rest of the profile:**
  - models (`splice`, `rows`);
  - nodes (`find`, `get`, `set`);
  - the bus interface (`snapshot`, `act`);
  - verbs: `invoke`, and `verb` for one the script implements, whose effect and answer rules (S-1, S-2) hold as for any verb.

Depends on `openmui.abi` (the C ABI's declarations and implementation) and `nlohmann.json`.
