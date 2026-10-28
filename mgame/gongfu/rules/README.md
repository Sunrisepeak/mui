# gongfu.rules

gongfu's build rule. A program's fighters become a module partition it carries, `<module>:gongfu_fighters`:

```cpp
// build.mcpp
import mcpp;
import gongfu.rules;
int main() { return gongfu::rules::fighters({.module = "acme.game", .ns = "acme::files"}) ? 0 : 1; }
```

```cpp
for (const acme::files::FighterFiles& f : acme::files::fighters)   // name, fighter, look, boxes
    auto fighter = gongfu::data::read_fighter(f.fighter, "…", f.boxes, "…");
```

- **What it carries:** every `<dir>/<name>.fighter.gongfu.toml`, with its `<name>.look.gongfu.toml` and the baked `<name>.boxes.gongfu.toml`. The directory is `fighters/` by default.
- **How it runs:** as one build action, `gongfu-studio embed`. It runs again whenever a file in the directory changes.
- **Stale bakes fail:** the action fails, and the build with it, when a boxes file is not what its fighter and look make now. The error names `gongfu-studio bake <dir>`.
- **Requirement:** the package declares it under `[build-dependencies]` with `host-module = true` ([showcase/fighting](../../../showcase/fighting) does).
