# openmui.host.web

openmui in the browser (D-84 §6.2): wasm32-emscripten, one thread (the
page's — Pages and Toy cannot send COOP/COEP, so there is no
SharedArrayBuffer).

| | |
|---|---|
| Canvas | `#canvas` (any selector), sized by the page; its CSS pixels are logical pixels, the device's pixel ratio the scale |
| Frames | the page's `requestAnimationFrame` (Emscripten's main loop); a frame only when one is wanted (D-70: input, a due timer, an animation, a lease's rate) |
| Input | pointer, touch, wheel, keys (by `KeyboardEvent.code`; text from the key), and the Gamepad API's standard pads through `GamepadReader` (spec §6.1); only the person's own events (`isTrusted`): a script's are not input (openas §2.7) |
| Input methods | a hidden textarea at the caret (`runtime.text_input_rect()`), named for the field: its composition is openmui's `PreeditEvent`, its result a `TextEvent`; a phone keyboard's letters and edits come the same way |
| Clipboard | as the page last knew it — a paste, or what the program copied — written through the Async Clipboard API; read again on return to the page where the person allowed it. The paste chord is the program's key; a paste by a menu is text |
| Semantic DOM mirror | after each batch of changes, the semantic tree as hidden elements over the canvas (`DomMirror`): the registry's ARIA role, the name, description, value and states as `aria-*`, a focusable node's `tabindex`, its box, the children in order, the focus. Only what changed is sent. A status is said in a polite live region, an alert dialog in an assertive one. A screen reader's focus and click come back as the focus moved and a press of the node's middle |
| `window.omui` | `snapshot`, `find`, `act`, `invoke`, `wait`, `capture`, `describe`, `changes` and `request` for the page's agents — `openmui.tree@0.1` as the desktop's agents speak it, served between frames. Promises of `{exitCode, data, errors, records}`. They come over the bus: openas decides (S-11), and the person's own actions are refused to them, whatever actor they name (S-10) |
| Store | the program's `Store` (`openmui.native.store`: `omui::store()` in C++, `openmui:native/store` in scripts) is the page's `localStorage`, under `omui/<app>/` |
| Drawing | `WebOptions::renderer`, as the desktop's host decides it (§6.3): the page's WebGPU, else its WebGL2 (`openmui.rhi.webgpu`, `openmui.rhi.webgl`), each frame drawn whole by the GPU renderer into a texture the canvas's size and presented; else the CPU renderer, only the rows that changed, put on the canvas (`putImageData`). `automatic` takes a GPU only when it is hardware (a software adapter is slower than the CPU renderer: it is asked first without the canvas, which keeps no context of a device that will not draw); `gpu`, `webgpu` and `webgl` insist. `renderer()` says who draws; `read_frame()` reads back what was shown. The CPU's pixels are the reference, the same bytes everywhere; a GPU's are within the renderer's tolerances of them |
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

```js
const { data } = await window.omui.find({ role: "button", name: "Save*" });
const r = await window.omui.act("notes/delete", "activate");   // r.exitCode 2: needs-answer
```

Off the Web the package builds with no page (`src/native/elsewhere`): the
same loop, stepped by hand — what its tests drive (`synthesize` and
`request` stand in for the page). The page's half is `src/js/omui_web.js`, an
Emscripten library `build.mcpp` gives the link. `tests/js` tests it under
node with a fake DOM, alone and with `tests/page_test.cpp` built for
wasm32-emscripten (the person composing, typing, pasting; a screen reader;
an agent through `window.omui`); `tools/ci/web.sh` runs both.

`tests/drawing_test.cpp`: without a page the CPU draws; in Chromium (`tools/ci/browser.sh`, with WebGPU, then WebGL2, the fonts preloaded) that device draws a page of boxes, borders, text in two scripts and a picture within the renderer's tolerances of the CPU's frame (4 levels at most, 64 pixels of 76 800 off by more than one: the rounded corners). Headless Chromium on SwiftShader loses a WebGPU device that presents to a canvas unless its Vulkan is SwiftShader's too, under which ANGLE's WebGL2 reads back wrong: each run has its own switches.

Not yet: IndexedDB for what is
larger than localStorage holds; the clipboard's other kinds than text.

| Package | |
|---|---|
| [toy](toy) | Bilibili Toy's SDK (`window.toy`) as a thin optional layer: lifecycle, cloud storage, leaderboards |
