# openmui.spec

The openmui specification and its machine-readable registry.

| Path | Contents |
|---|---|
| [SPEC.md](SPEC.md) | the normative text, version 0.1 |
| [registry/](registry/) | the source of truth: roles, states, effects, origins, actions, extensions, profiles, motion, expressions, the effect-shader interface, the C ABI, the Core component contracts |
| [include/openmui.h](include/openmui.h) | the C ABI (generated) — the only header of openmui |
| [src/](src/) | module `openmui.spec`: the value types (`src/generated/` is generated) |
| [docs/registry.md](docs/registry.md) | the registry as reference tables (generated) |
| [stubs/](stubs/), [schema/](schema/) | Core builders for TS / Python / Lua and the view-document schema (generated) |
| [conformance/](conformance/) | the conformance suite: S, A, B, C |

Regenerate after changing the registry:

```bash
mcpp build -p openmui.gen
<built omui-gen> registry spec/registry --out spec
```

`mcpp test -p openmui.gen` fails when a generated file is out of date. Standard library only; `mcpp test -p openmui.spec` runs the value-type tests.
