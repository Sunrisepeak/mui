# mlibs.utils

Small domain-agnostic tools that are not libraries of their own yet, one directory each under `src/` (convergence plan §9.3). Each keeps its module name, so a tool that grows into its own library moves out with no `import` changed, only the dependency key (`mlibs.utils` → its own).

| Tool | Module | |
|---|---|---|
| [hash](src/hash/README.md) | `mlibs.hash` | FNV-1a (64 and 32 bit): determinism hashes, content hashes, cache keys, generated ids — the one implementation (lint `hash`) |
| [base64](src/base64/README.md) | `mlibs.base64` | RFC 4648 base64: pictures in JSON and SVG, read back by the command line — the one implementation |
| [log](src/log/README.md) | `mlibs.log` | Categories, filters, sinks, record history and invariants |
| [trace](src/trace/README.md) | `mlibs.trace` | Spans, counters and marks; Chrome trace JSON; per-span statistics; near-free while off |
| [testing](src/testing/README.md) | `mlibs.testing` | The checks test programs share: suites, `check*` with source locations, pinned hashes |

Depends on: the standard library alone (an independent component, D-55); it is the bottom of the base layer, so any package may use it.

**Every tool is built**, without a feature per tool. The tools are small and need nothing but the standard library, while a feature per tool would give this package a different feature set for each selection of members: mcpp then compiles it again, and everything that imports it, whenever a command selects other members than the last one did (measured with mcpp 2026.11.2.2: `build --workspace`, then `test`, then `build` again recompiled the whole graph each time). `mlibs.testing` is for test programs only: lint `testing` allows `import mlibs.testing` under `tests/` alone, so no artifact carries it.

**Admission**: domain-agnostic, small, no third-party dependency to isolate, not published on its own. **Leaving**: the graduation criteria of [modules](../../README.md), or a third-party dependency of its own.
