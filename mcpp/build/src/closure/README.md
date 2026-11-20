# omui.build.closure

What a TypeScript application carries, derived from what it wrote (B7b; the [closure design](../../../../.agents/docs/2026-11-19-closure-derivation-design.md), D-251). The application's build program calls it, and its program takes the player's `closure` entry:

```cpp
// build.mcpp
omui::build::closure({.files = {"app", "design/main.replay.xdef.json"}});
```

```toml
[targets.my-app]
main = { from = "omui.entry.player/closure" }
```

`omui-gen closure` reads the static imports of the application's scripts, and of every step its replay runs, without running them (`mlibs::ts::imports`). It writes two things:
- a unit of the build that defines `omui_application_components()`, which registers the base components named and no others (`omui::register_base_named<…>()`), markdown only when a script names it, and an official native module (`fuzzy`) only when a script imports it;
- `closure.json` beside the build's outputs, saying what was taken and where.

Everything official stays (`omui::register_official()`) in three cases, and `closure.json` says why:
- a script takes everything of `openmui` (`import *`, `export *`, `import()`);
- the designer is linked (`omui.entry.player/design`, development);
- `omui.config.ts` opens generation to components or surfaces.
