# openmui.gen (`omui-gen`)

The generator: `spec/registry` and `*.contract.omui.toml` to every derived artifact — `openmui.h`, the `openmui.spec` partitions, reference tables, TS / Python / Lua builders and the view-document schema (implementation plan §4.2).

```bash
omui-gen registry spec/registry --out spec [--check] [--json]
omui-gen stubs <contract>... --registry spec/registry --module <name> --out <dir> [--check]
omui-gen check <contract>... --registry spec/registry [--json]
```

Every problem is reported with file, line and column, a message and a hint; `--json` gives the same as JSON. `--check` writes nothing and fails when an output is out of date.
