# openmui.build.embed

The build plugin for data files a program carries (D-73's embed plugin). Each file becomes a partition holding its text (`omui-gen embed`), so a shipped program has its files without reading any.

```cpp
// build.mcpp
import mcpp;
import openmui.build.embed;
int main() {
    return openmui::build::embed({.module = "openmui.theme.styles", .ns = "omui::themes", .dir = "styles",
                                  .suffix = ".style.omui.toml", .tag = "style"}) ? 0 : 1;
}
```

- `embed()`: each `<stem><suffix>` in `dir` becomes `<module>:<stem>_<tag>` (`-` and `.` in the stem become `_`). With `.index = true`, `<module>:<tag>_files` is also made: a table of every file by name with its text.
- `themes()`: for theme packages (`kind = "theme"`). Each `themes/*.theme.omui.toml` becomes `<module>:<stem>_theme`, which the package adds to the program's theme library.
- `behaviors()`: for canvas programs. Each `behaviors/*.behavior.omui.toml` becomes `<module>:<stem>_behavior`, which the program loads with `omui::world::load_behaviors`.
