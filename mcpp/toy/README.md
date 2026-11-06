# omui.build.toy

The build plugin for Bilibili Toy (D-84 §6.5): the pack format `dist-toy`. A program built for wasm32-emscripten becomes a Toy package containing:
- a small `index.html`;
- the program and its fonts, under content-hashed names;
- relative paths throughout;
- hash routes that become the program's flags.

With `.sdk = false` the page does not load Toy's SDK, and it is a plain static page. omui's site is built this way.

```cpp
// build.mcpp
import mcpp;
import omui.build.toy;
int main() { return openmui::build::dist_toy({.target = "omui-fight", .title = "格斗"}) ? 0 : 1; }
```

```
mcpp pack -p <package> --target wasm32-emscripten --release --pkg-format dist-toy
```

Only the package being packed makes one: a package that is a dependency of the one being packed (the fighting game under the site) leaves its own `dist_toy` alone. The plugin asks `omui.build.plan`'s `is_packed()`.

The package lands in `<out_dir>/toy/<target>/`, and the list of its contents beside it in `<target>.toy.omui.json`. `tools/ci/toy.sh` checks it with Toy's `toy_doctor.py`. Publishing (`toy create`) is the maintainer's job.

Language fonts can keep a small amount of initial coverage:

```cpp
openmui::build::dist_toy({
    .target = "omui-site", .fonts = {"Inter", "JetBrainsMono", "NotoSansSC"}, .sdk = false,
    .font_languages = {{"NotoSansSC", {"zh"}}},
    .font_initial = {{"NotoSansSC", "中文"}}
});
```

An English visit loads the two-character face before main and fetches the regular Chinese face after presentation. A Chinese visit loads the regular face before main and skips the initial asset. Both use the same metrics and glyph ids. The initial cmap declares only its own characters, so the rest can come from the regular face when it arrives. When a full-font subset is requested too, the initial characters are included in it.

Applications with their own locale routing may set `window.omuiFontLanguage` before the loader's preRun; it takes precedence over the route/browser heuristic. omui's static bootstrap sets it to the language the selected page actually displays.

While language fonts are deferred, `/fonts/.partial` marks that the canonical sources are incomplete. A later font updates `/fonts/<name>.ttf` and exposes a `/later/fonts/` alias for the Web host to add to its existing fallback chain; this uses one MEMFS file. When the font batch completes the partial marker is removed. Tools that create an independent font collection, such as the site's reference replay, can wait for that transition and then read the complete canonical files.
