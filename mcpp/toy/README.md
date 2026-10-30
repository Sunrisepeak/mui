# openmui.build.toy

The build plugin for Bilibili Toy (D-84 §6.5): the pack format `dist-toy`. A program built for wasm32-emscripten becomes a Toy package containing:
- a small `index.html`;
- the program and its fonts, under content-hashed names;
- relative paths throughout;
- hash routes that become the program's flags.

With `.sdk = false` the page does not load Toy's SDK, and it is a plain static page. openmui's site is built this way.

```cpp
// build.mcpp
import mcpp;
import openmui.build.toy;
int main() { return openmui::build::dist_toy({.target = "omui-fight", .title = "格斗"}) ? 0 : 1; }
```

```
mcpp pack -p <package> --target wasm32-emscripten --release --pkg-format dist-toy
```

Only the package being packed makes one: a package that is a dependency of the one being packed (the fighting game under the site) leaves its own `dist_toy` alone. The plugin asks `openmui.build.plan`'s `is_packed()`.

The package lands in `<out_dir>/toy/<target>/`, and the list of its contents beside it in `<target>.toy.omui.json`. `tools/ci/toy.sh` checks it with Toy's `toy_doctor.py`. Publishing (`toy create`) is the maintainer's job.
