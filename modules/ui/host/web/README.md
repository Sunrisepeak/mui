# openmui.host.web

openmui in the browser (D-84 §6.2): wasm32-emscripten, one thread (the
page's — Pages and Toy cannot send COOP/COEP, so there is no
SharedArrayBuffer).

| | |
|---|---|
| Canvas | `#canvas` (any selector), sized by the page; its CSS pixels are logical pixels, the device's pixel ratio the scale |
| Frames | the page's `requestAnimationFrame` (Emscripten's main loop); a frame only when one is wanted (D-70: input, a due timer, an animation, a lease's rate) |
| Input | pointer, touch, wheel, keys (by `KeyboardEvent.code`; text from the key), and the Gamepad API's standard pads through `GamepadReader` (spec §6.1) |
| Drawing | the CPU renderer, only the rows that changed, put on the canvas (`putImageData`); WebGL2 and WebGPU later (§6.3) |
| Fonts | `/fonts` in the page's file system, filled by the page before `main` (`dist-toy` writes the loader) |
| Visibility | the page hidden is the window's focus lost; `on_visibility` for the application (Toy's lifecycle) |

`omui::run` uses it on the Web: the umbrella package's `openmui:host`
partition and `src/host/web/shown.cpp` are chosen by the manifest for
`wasm32-emscripten`, so any openmui application builds for the browser
unchanged. The page's loop takes over and `main` does not return.

```cpp
import openmui.host.web;

omui::Web w({.title = "Notes"});
w.mount("main", notes());
return w.run();
```

Off the Web the package builds with no page (`src/native/elsewhere`): the
same loop, stepped by hand — what its tests drive. The page's half is
`src/js/omui_web.js`, an Emscripten library `build.mcpp` gives the link.

Not yet: the hidden textarea for input methods, the semantic DOM mirror,
`window.omui` for agents, the clipboard, WebGL2/WebGPU, a `Store` over
localStorage.

| Package | |
|---|---|
| [toy](toy) | Bilibili Toy's SDK (`window.toy`) as a thin optional layer: lifecycle, cloud storage, leaderboards |
