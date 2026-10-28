# modules

The libraries openmui is made of, in layers. A package depends only on its own layer and the layers below it; `tools/lint` checks every dependency.

| Layer | What it is | Depends on |
|---|---|---|
| [base](base) | Foundations: memory, geometry, Unicode, tracing, sessions, pipelines, images, and the animation engine hanim | the standard library, third-party libraries |
| [gfx](gfx) | Graphics: the GPU abstraction, shaders, the CPU rasteriser and the renderer | base |
| [content](content) | Text, layout, Markdown and internationalisation | base, gfx |
| [ui](ui) | openmui itself: the runtime, views, semantics, agents, themes and hosts | the layers above, [spec](../spec) |

`base`, `gfx` and `content` are independent components: none of them knows of openmui, and each can be used, and published, on its own (D-55). openmui never depends on a game engine; an engine uses openmui as its interface (D-82).

A package's directory is its name: `openmui.rhi.sdl` lives in `gfx/rhi/sdl`, `hanim` in `base/hanim` (D-80).
