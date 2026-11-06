# modules/base

The lowest layer group: **domain-agnostic foundations** (`mlibs.*`) that know nothing of interfaces, graphics or games.

Depends on: the standard library and third-party libraries. Used by: every other layer.

What belongs here: general-purpose libraries with no domain semantics. Small tools that are not yet a library of their own go to [utils](utils) as directories of one package (`mlibs.utils`, module names such as `mlibs.hash` unchanged; every tool is built, see its README), and move out into their own library once they grow a cohesive API or need their own third-party dependencies. Domain code does not belong here: it lives in the project that owns it under `modules/projects/` (target layout, see [modules](../README.md)). `hanim.core` and `xengine.simulation` below are scheduled to move to their projects in milestone M1.

| Package | |
|---|---|
| [control](control) | Independent openas policy, trusted sessions and invocation authority; shared by UI and standalone engines |
| [hanim](hanim) | Independent hanim.core: deterministic time, values, curves and prepared shape morphs; the product umbrella lives in gfx/hanim |
| [geom](geom) | 2D geometry: points, rectangles, colours, affine transforms, pixel snapping |
| [memory](memory) | Frame arena, generational handles, slot maps, `FunctionRef` |
| [unicode](unicode) | Line breaking (UAX #14) and segmentation (UAX #29) without ICU |
| [session](session) | Recorded sessions (`*.session.omui.jsonl`): timestamped, seekable, mergeable |
| [xengine.simulation](simulation) | Independent fixed-step clock, owned checkpoint/input replay, and optional integer cellular rules |
| [pipeline](pipeline) | Pipelines of stage plugins over typed resources: checked, deterministic, traced, replaceable while running |
| [image](image) | Image buffers, premultiplication, PNG, animated PNG and WebP |
| [math](math) | Deterministic mathematics: elementary functions with the same bits everywhere (`det`), splitmix64 (`random`) |
| [utils](utils) | `mlibs.utils`: small tools, one directory each — hash (FNV-1a), log (categories, sinks, invariants), trace (spans, Chrome trace), testing (the checks of test programs) |

Start with [hanim](hanim/README.md).

The current canonical packages are `mlibs.control`, `mlibs.utils` (modules
`mlibs.hash`, `mlibs.log`, `mlibs.trace`, `mlibs.testing`), `mlibs.math`,
`mlibs.geom`, `mlibs.memory`, `mlibs.unicode`, `mlibs.session`,
`mlibs.pipeline` and `mlibs.image`. Except control, these retain their old
`openmui.*` import within the same package until milestone M1 removes them. Other foundations are still being
migrated; use their manifest identity. `mlibs` is a grouping, not an umbrella
dependency. See the [migration plan](../../.agents/docs/2026-11-05-product-namespace-migration-design.md).
