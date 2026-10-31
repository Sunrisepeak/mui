# openmui.build.contracts

The build plugin for contracts (component-binding §2.3, §8, §10.3). A package writes its interface once, as contracts, and the plugin makes the rest when the package builds.

| from `contract/*.contract.omui.toml` | made by | goes |
|---|---|---|
| `<name>_contract.cppm`: props, choice enums, options, the CRTP base | `omui-gen cpp --part contract` | compiled, as the partition `<module>:<name>_contract` |
| `<name>_builder.cpp`: the builder | `omui-gen cpp --part builder` | compiled, as an implementation unit of the module |
| `<module>.d.ts`, `.pyi`, `.lua`, `.components.json` | `omui-gen stubs` | shipped beside the package |

```cpp
// build.mcpp
import mcpp;
import openmui.build.contracts;
int main() { return openmui::build::components({.module = "acme.ui", .ns = "acme::ui"}) ? 0 : 1; }
```

A native-module package (component-binding §8, `kind = "native"`) does the same for `native/*.native.omui.toml`:

| from `native/*.native.omui.toml` | made by | goes |
|---|---|---|
| `<name>_native.cppm`: records, the functions to implement, `define()` | `omui-gen native --part cpp` | compiled, as the partition `<module>:<name>_native` |
| `<module>.natives.d.ts`, `<name>.pyi`, `<module>.natives.lua` | `omui-gen native --part stubs` | shipped beside the package |

```cpp
int main() { return openmui::build::natives({.module = "acme.finder"}) ? 0 : 1; }
```

The package implements the functions the partition declares. Its program calls `<name>::define()` once, before scripts run. The registry is openmui's `spec/registry`, found beside the plugin unless `registry` names another.
