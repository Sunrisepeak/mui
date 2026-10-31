# openmui.native

The native modules of a program (component-binding §8): functions written in C++ behind a `*.native.omui.toml` contract, which scripts call. It is like React Native's TurboModules or Tauri's commands, but nothing is written by hand between the languages.

```toml
# native/fuzzy.native.omui.toml
[native.fuzzy.functions.rank]
args    = { query = "string", candidates = "string[]", limit = "u32?" }
returns = "Match[]"
async   = true
```

- **The contract is the interface.** `omui-gen native` (or `openmui.build.contracts`' `natives()` in a build) generates a partition with the records, the declarations of the functions the package implements, their conversions, and `define()`.
- **`define()` registers the module here, once.** A host calls it before scripts run. A second definition of a name is refused.
- **Scripts call it through the C ABI.** `omuiNativeCatalog` lists the modules; `omuiNativeCall` calls a function with JSON arguments, positional in the contract's order or by name. The bindings are made from the catalogue:
  - JavaScript/TypeScript: `import { fuzzy } from "openmui:native/fuzzy"`. An `async` function returns a promise and also has a `…Sync` form.
  - Python: `from openmui.native import fuzzy`.
  - Lua: `omui.native.fuzzy.rank(…)`.
- **Arguments are checked here, in one place**, against the contract's types:
  - the types are `bool`, `i32`, `u32`, `i64`, `f64`, `string`, `any`, a record of the module, `T[]` and `T?`;
  - optional arguments come last;
  - a failed check is refused with `invalid-argument`, and the message says where (`` `candidates[1]` is the number 3, not a string ``);
  - an unknown function or module is `not-found`;
  - an implementation's own `std::unexpected("why")` comes back as `failed`.
- **Threads.** A call runs on the caller's thread, since a native touches no runtime state. An `async` one runs on a worker of the script's session, and its answer comes back on the script thread.

## Inside

- [fuzzy](fuzzy) (`openmui.native.fuzzy`): fuzzy matching for scripts, `openmui.fuzzy` behind a native-module contract.
- [store](store) (`openmui.native.store`): the program's own small storage, the `Store` plugin a host gives (the page's localStorage on the Web, memory elsewhere), for C++ and scripts.
