# mlibs.testing

The checks openmui's test programs share: `Suite` (named cases, the exit code is the verdict), `check` / `check_eq` / `check_bits` / `check_near` with `std::source_location`, and `Hash` for pinned determinism hashes. No macros. It is a directory of `mlibs.utils`; lint `testing` keeps `import mlibs.testing` in test programs (`tests/`), so no artifact carries it.

The package and public import are `mlibs.testing`. The legacy
`import openmui.testing;` and `omui::testing` API remain source-compatible
within this same package; rebuild C++ consumers. Both entry points share the
same types, implementation and state. No extra compatibility package is needed.
See the [namespace migration guide](../../../../../.agents/docs/2026-11-05-product-namespace-migration-design.md#5-n1首组五个基础库的实际迁移).
