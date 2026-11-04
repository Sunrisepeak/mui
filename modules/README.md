# modules

Reusable libraries shared by the project's independent products, organized by dependency layer. A package depends only on its own layer and the layers below it; `tools/lint` checks every dependency. Product roles and selective composition are described in the [top-level architecture](../.agents/docs/2026-11-04-multi-engine-product-architecture-design.md). The [system/subsystem review proposal](../.agents/docs/2026-11-05-system-subsystem-architecture-and-api-design.md) includes a target code tree and staged migrations; proposed packages are not existing capabilities.

| Layer | What it is | Depends on |
|---|---|---|
| [base](base) | Foundations: memory, math, geometry, Unicode, logging, tracing, sessions, pipelines, images, and hanim.core | the standard library, third-party libraries |
| [gfx](gfx) | Graphics: the GPU abstraction, shaders, CPU rasteriser, renderer and independent hanim object layer | base |
| [content](content) | Text, layout, Markdown and internationalisation | base, gfx |
| [media](media) | Independent audio, video and timeline libraries and their device/codec packages | base, gfx, content |
| [ui](ui) | The omui UI implementation: runtime, views, semantics, agents, themes and hosts | the layers above, [spec](../spec) |

`base`, `gfx`, `content` and `media` are independent components: they do not depend on the UI runtime or UI specification, and each can be used and published on its own (D-55). The UI framework never depends on a game engine; games can consume its public UI APIs (D-82).

A package's directory is its name: `openmui.rhi.sdl` lives in `gfx/rhi/sdl`, `hanim.core` in `base/hanim/core`, and `hanim.object` in `gfx/hanim/object` (D-80). The `hanim` product umbrella lives in `gfx/hanim` and exports core, objects, library, effects, programs and the Engine; renderer adapters are opt-in. `base/hanim` is a grouping directory for the independent core.
