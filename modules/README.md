# modules

Every engine, framework and library of the project lives here; the repository's top level holds their consumers (site, showcases, examples), the specification, build plugins, entries and tools. A package depends only on its own layer and the layers below it; `tools/lint` checks every dependency.

## Two kinds of group

Every group directory has a README that states what belongs in it and lists its packages.

| Group | What it holds | Admission |
|---|---|---|
| **Layer groups** — [base](base), [gfx](gfx), [content](content) | `mlibs.*`: domain-agnostic libraries, pure abstract interfaces (`mlibs.rhi`) and general toolchains (`mlibs.shader`); small tools in `base/utils` | general purpose, no domain semantics |
| **Projects** — [projects](projects) | Branded engines, frameworks and modules that are mature and complex enough to stand as their own project or repository, **each describable in one sentence and with a few clear signature features that lead the industry or are designed for what comes next**: `hanim`, `omui` (with its components and language bindings), `xengine`, `xrender`, `gongfu`. A project holds its own not-yet-graduated subsystems (xengine: simulation, audio, video, timeline, world, input, physics, net) | the graduation criteria below, or named by the maintainer and completed against them |

Rule of thumb: general things go to `mlibs`, concrete implementations go to the project that owns them. A name says who owns a package, a directory says which group it is in, and the layer comes from the lint's path-to-layer table, so one project may span layers (`hanim.core` is base, `hanim` is gfx).

**Graduation** (an xengine subsystem or an `mlibs` library becoming its own project under `projects/`): a one-sentence definition (the first line of its README), signature features with evidence (a benchmark against the best alternatives, or a real task that only this design makes possible), at least two independent consumers, a frozen v1 API, ops and skill files, a CTS, documentation and benchmarks. A tool in `base/utils` becomes its own library when it grows into a cohesive API or needs its own third-party dependencies.

## Current layout and migration

The layout above is the target of milestone M1 in the [top-level convergence plan](../.agents/docs/2026-11-06-top-level-assessment-convergence-design.md) (§9). [hanim](projects/hanim) and [xrender](projects/xrender) are in `projects/` already; the rest still uses layer directories:

| Layer (today) | What it is | Depends on | Target |
|---|---|---|---|
| [base](base) | Foundations (`mlibs.*`, small tools in `utils`), plus `xengine.simulation` | the standard library, third-party libraries | mlibs stay; simulation → `projects/xengine` |
| [gfx](gfx) | GPU abstraction and shaders, plus simulation's GPU adapter | base | `mlibs.rhi`, `mlibs.shader` stay; the adapter → `projects/xengine` |
| [content](content) | Text, layout, Markdown, internationalisation | base, gfx | stays |
| [media](media) | Audio, video and timeline | base, gfx, content | → `projects/xengine` |
| [ui](ui) | The omui UI implementation | the layers above, [spec](../spec) | → `projects/omui`, together with the top-level `components/` and `lang/` |

A package's directory is its name (D-80): the leaf directory is the last segment of the package name; grouping directories only group.
