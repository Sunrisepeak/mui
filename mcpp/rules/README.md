# openmui.rules

openmui's build rule (component-binding §2.3, §10.3). A component package
writes its interface once, as contracts, and the rule makes the rest when it
builds:

| from `contract/*.contract.omui.toml` | made by | goes |
|---|---|---|
| `<name>_contract.cppm` — props, choice enums, options, the CRTP base | `omui-gen cpp --part contract` | compiled, as the partition `<module>:<name>_contract` |
| `<name>_builder.cpp` — the builder | `omui-gen cpp --part builder` | compiled, an implementation unit of the module |
| `<module>.d.ts`, `.pyi`, `.lua`, `.components.json` | `omui-gen stubs` | shipped beside the package |

```toml
[build-dependencies]
openmui.rules = { path = "…/mcpp/rules", host-module = true }
```

```cpp
// build.mcpp
import mcpp;
import openmui.rules;
int main() { return openmui::rules::components({.module = "acme.ui", .ns = "acme::ui"}) ? 0 : 1; }
```

A native-module package (component-binding §8, `kind = "native"`) does the
same for `native/*.native.omui.toml`:

| from `native/*.native.omui.toml` | made by | goes |
|---|---|---|
| `<name>_native.cppm` — records, the functions to implement, `define()` | `omui-gen native --part cpp` | compiled, as the partition `<module>:<name>_native` |
| `<module>.natives.d.ts`, `<name>.pyi`, `<module>.natives.lua` | `omui-gen native --part stubs` | shipped beside the package |

```cpp
// build.mcpp
int main() { return openmui::rules::natives({.module = "acme.finder"}) ? 0 : 1; }
```

The package implements the functions the partition declares, and its program
calls `<name>::define()` once before scripts run.

A theme package (`kind = "theme"`) carries its `themes/*.theme.omui.toml`
embedded: `themes({.module = "acme.themes"})` makes each file the partition
`<module>:<stem>_theme` holding its text (`omui-gen embed`), which the package
adds to the program's theme library.

The generator is a dependency built for the build machine
(`tools = ["omui-gen"]`); every action declares it, the contract and the
registry as inputs, so changing any of them regenerates, and nothing else
does. Generated module units are declared with what they provide and import,
because a file that does not exist yet cannot be scanned. `plan` and `submit`
are separate, so a build program can adjust an edge before handing it over.

`tests/rules` is a third-party package built this way.
