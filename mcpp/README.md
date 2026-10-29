# mcpp/ — openmui's build plugins

Build-system code only (AGENTS.md). Each plugin is a package of its own that does one job, and a package's `build.mcpp` imports only the plugins its situation needs. A component package uses contracts, a theme package uses embed, and a game for Bilibili Toy uses contracts, embed and toy. Every plugin is a host module, built for the build machine:

```toml
[build-dependencies]
openmui.build.contracts = { path = "…/mcpp/contracts", host-module = true }
openmui.build.embed     = { path = "…/mcpp/embed", host-module = true }
```

| Package | When you need it | What it gives `build.mcpp` |
|---|---|---|
| [`openmui.build.contracts`](contracts) | Your package writes its interface as contracts: components (`contract/*.contract.omui.toml`) or native modules (`native/*.native.omui.toml`) | `components()`, `natives()`: C++ partitions and builders, script stubs, the view-document schema |
| [`openmui.build.embed`](embed) | Your program carries data files instead of reading them: themes, canvas behaviours, styles, locales, sounds, documents | `embed()`, `themes()`, `behaviors()`: each file as a partition holding its text, and optionally a table of them all |
| [`openmui.build.toy`](toy) | Your program ships as a Bilibili Toy package, or as a static page | `dist_toy()`: the `dist-toy` pack format |
| [`openmui.build.plan`](plan) | You write a plugin of your own | What the plugins share: a planned `edge`, `submit()`, `generator()`, UTF-8 path text, the files a plugin reads (each plugin re-exports it) |

Plugins for one subsystem live with it, not here. Examples are `openmui.shader.rules` (Slang shaders, `modules/gfx/shader/rules`) and `gongfu.rules` (fighters, `mgame/gongfu/rules`).

Every edge declares the generator and the files it reads as its inputs, so changing any of them regenerates and nothing else does. Generated module units are declared with what they provide and what they import, because a file that does not exist yet cannot be scanned. `plan…()` and `submit()` are separate, so a build program can adjust an edge before handing it over. `tests/plugins` holds a third-party package built with each plugin, and its tests.
