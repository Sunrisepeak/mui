# mcpp/ — openmui's build plugins

Build-system code only (AGENTS.md). The plugins are one package, `omui.build` ([`build`](build)): they share their one dependency (the generator, omui.gen) and are small, so they are not split by file (D-99). Each plugin is a module of its own that does one job, and a package's `build.mcpp` imports only the modules its situation needs: a component package uses contracts, a theme package uses embed, and a game for Bilibili Toy uses contracts, embed and toy. The package is a host module, built for the build machine:

```toml
[build-dependencies]
omui.build = { path = "…/mcpp/build", host-module = true }
```

| Module | When you need it | What it gives `build.mcpp` |
|---|---|---|
| [`omui.build.contracts`](build/src/contracts) | Your package writes its interface as contracts: components (`contract/*.contract.xdef.ts`) or native modules (`native/*.native.xdef.ts`) | `components()`, `natives()`: C++ partitions and builders, script stubs, the view-document schema |
| [`omui.build.embed`](build/src/embed) | Your program carries data files instead of reading them: themes, canvas behaviours, styles, locales, sounds, documents | `embed()`, `themes()`, `behaviors()`: each file as a partition holding its text, and optionally a table of them all |
| [`omui.build.toy`](build/src/toy) | Your program ships as a Bilibili Toy package, or as a static page | `dist_toy()`: the `dist-toy` pack format |
| [`omui.build.plan`](build/src/plan) | You write a plugin of your own | What the plugins share: a planned `edge`, `submit()`, `generator()`, UTF-8 path text, the files a plugin reads (each plugin re-exports it) |

Plugins for one subsystem live with it, not here. Examples are `openmui.shader.rules` (Slang shaders, `modules/gfx/shader/rules`) and `gongfu.rules` (fighters, `modules/projects/gongfu/rules`).

Every edge declares the generator and the files it reads as its inputs, so changing any of them regenerates and nothing else does. Generated module units are declared with what they provide and what they import, because a file that does not exist yet cannot be scanned. `plan…()` and `submit()` are separate, so a build program can adjust an edge before handing it over. `tests/plugins` holds third-party packages built with the plugins, and their tests; `import omui.build;` gives every plugin at once.
