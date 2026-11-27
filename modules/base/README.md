# modules/base

The lowest layer group: **domain-agnostic foundations** (`mlibs.*`) that know nothing of interfaces, graphics or games.

Depends on: the standard library and third-party libraries. Used by: every other layer.

What belongs here: general-purpose libraries with no domain semantics. Small tools that are not yet a library of their own go to [utils](utils) as directories of one package (`mlibs.utils`, module names such as `mlibs.hash` unchanged; every tool is built, see its README), and move out into their own library once they grow a cohesive API or need their own third-party dependencies. Domain code does not belong here: it lives in the project that owns it under `modules/projects/` (target layout, see [modules](../README.md)). `hanim.core` and the solvers (`meng.simulation`, later `meng.dynamics`) are in their projects ([hanim](../projects/hanim), [meng](../projects/meng)).

| Package | |
|---|---|
| [build](build) | `mlibs.build`: the foundation's build plugins — planned edges, a tool built for the build, data files carried in the program as module partitions (`xdef embed`); omui's build plugins build on it (D-73, D-231) |
| [delivery](delivery) | `mlibs.delivery` (preview): a built directory as a static host sends it — each file's bytes and gzip bytes, budgets over patterns and their verdicts, the delivery manifest; a tool's shared library (0.1.0 plan RL7) |
| [compose](compose) | `mlibs.compose` (preview): what a program is made of, chosen at its root — slots (typed, versioned contracts with one owner), providers, and compositions that say what is installed and, for what is absent, why and the way out (D-238) |
| [cts](cts) | `mlibs.cts`: what a conformance suite is written with, on mtest's conformance kind — cases with stable ids and levels S/A/B declared against a subject, passed/failed/unsupported (from capabilities only), comparisons with evidence, the preparation contract; suites live with their contract's owner, the runner and reports are mtest's |
| [dynamics](dynamics) | `mlibs.dynamics`: the generic solver core — swept collision, half-planes and one-way ledges, XPBD soft bodies, what a body is doing; deterministic, stepped on `mlibs.simulation` (hanim's engine and the engine's `meng.dynamics` build on it; D-133, D-231) |
| [simulation](simulation) | `mlibs.simulation`: the stepping substrate — fixed-step clocks, timelines of checkpoints and inputs (seek, replay, rollback), simulation tiers; no solver (D-133) |
| [control](control) | Independent openas policy, trusted sessions and invocation authority; shared by UI and standalone engines |
| [agent](agent) | `mlibs.agent`: the agent kernel (D-173) — where agents find a program and talk to it (a named local endpoint, lines in and out, sessions served at the owner's safe point), for any subject: an interface, a world, a renderer's frame. Authority is `control`'s; recorded sessions (`*.session.xdef.jsonl`, feature `session`) |
| [geom](geom) | 2D geometry: points, rectangles, colours, affine transforms, pixel snapping |
| [memory](memory) | Frame arena, generational handles, slot maps, `FunctionRef` |
| [unicode](unicode) | Line breaking (UAX #14) and segmentation (UAX #29) without ICU |
| [pipeline](pipeline) | Pipelines of stage plugins over typed resources: checked, deterministic, traced, replaceable while running |
| [json](json) | `mlibs.json`: JSON documents with their places — nodes with JSON pointers, problems at line and column, did-you-mean; how the data files' JSON snapshots are read |
| [project](project) | `mlibs.project`: a project as the tools see it — its packages from their manifests — and its health: the general rules every project keeps and the profiles owners add (D-205, D-207); `omui doctor` runs them |
| [ts](ts) | `mlibs.ts`: TypeScript with no tool — types blanked to the JavaScript under them (lines and columns kept), declared data modules evaluated deterministically to JSON; the tool kernel's, a kind's define module its owner's (D-209) |
| [xdef](xdef) | `mlibs.xdef`: declared data's container — file names, `$format`, the kinds' registry each owner keeps in its manifest, canonical snapshots, and `xdef`, the reference evaluator and checker (D-209) |
| [image](image) | Image buffers, premultiplication, PNG, animated PNG, WebP and JPEG |
| [math](math) | Deterministic mathematics: elementary functions with the same bits everywhere (`det`), splitmix64 (`random`) |
| [mpal](mpal) | `mpal`: the services an operating system gives an application (notifications first), one contract on every platform, above `mcpplibs.os` (openkal → os → mpal); a package of its own, meant to leave the repository once its contracts settle |
| [utils](utils) | `mlibs.utils`: small tools, one directory each — hash (FNV-1a), log (categories, sinks, invariants), trace (spans, Chrome trace), testing (the checks of test programs) |

Start with [utils](utils/README.md) and [math](math/README.md).

The current canonical packages are `mlibs.control`, `mlibs.utils` (modules
`mlibs.hash`, `mlibs.log`, `mlibs.trace`, `mlibs.testing`), `mlibs.math`,
`mlibs.geom`, `mlibs.memory`, `mlibs.unicode`,
`mlibs.pipeline` and `mlibs.image`. Except control, these retain their old
`openmui.*` import within the same package until milestone M1 removes them. Other foundations are still being
migrated; use their manifest identity. `mlibs` is a grouping, not an umbrella
dependency. See the [migration plan](../../.agents/docs/2026-11-05-product-namespace-migration-design.md).

## Labels in 0.1 (release plan §2.3)

mlibs is this layer with [gfx](../gfx) and [content](../content); each package's manifest says its label (`maturity`).

| Capability | Label | Packages | Evidence |
|---|---|---|---|
| The agent kernel: endpoints, sessions, subjects, history; openas policy | stable | `mlibs.agent`, `mlibs.control` | [session_test](agent/tests/session_test.cpp), [the contract's suite](agent/cts) |
| Data: JSON with pointers, xdef snapshots, TypeScript evaluated to data | stable; the xdef command-line tool is preview | `mlibs.json`, `mlibs.xdef`, `mlibs.ts`, `mlibs.xdef.tool` | [json_test](json/tests/json_test.cpp), [xdef_test](xdef/tests/xdef_test.cpp) |
| Mathematics, geometry, memory, hashing, logs and traces, stepping | stable | `mlibs.math`, `mlibs.geom`, `mlibs.memory`, `mlibs.utils`, `mlibs.simulation`, `mlibs.pipeline` | [math_test](math/tests/math_test.cpp), [simulation_test](simulation/tests/simulation_test.cpp) |
| The device interface and shaders | stable | `mlibs.rhi`, `mlibs.shader` | [rhi_test](../gfx/rhi/tests/rhi_test.cpp), [the device suite](../gfx/rhi/cts) |
| Text, layout, Markdown, locales | stable | `mlibs.text`, `mlibs.layout`, `mlibs.markdown`, `mlibs.i18n` | [text_test](../content/text/tests/text_test.cpp), [layout_test](../content/layout/tests/layout_test.cpp), [Markdown's conformance_test](../content/markdown/tests/conformance_test.cpp) |
| Workspaces as projects | preview | `mlibs.project` | [the project model](project) |

## Known limitations

Each names the level it is solved at (D-179: foundation, part, specialised, application).

- **Test helpers outside mtest** (foundation): `mlibs.testing` keeps three helpers (kept output directories among them) until mtest has their equivalents, then goes.
- **Workspaces as projects** (foundation): `mlibs.project` is preview; its model may change in 0.1.x.

## Closure exceptions

- **5**: mlibs is libraries without operations of their own: the agent kernel (`mlibs.agent`) hosts the other projects' catalogs, and its one program, `xdef`, is a small command declared with `mcpplibs.cmdline` (AGENTS §4: no catalog, no generated command line).
