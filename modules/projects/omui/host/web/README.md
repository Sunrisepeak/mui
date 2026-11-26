# omui.host.web

openmui in the browser (D-84 §6.2): wasm32-emscripten, one thread (the
page's — Pages and Toy cannot send COOP/COEP, so there is no
SharedArrayBuffer).

| | |
|---|---|
| Canvas | `#canvas` (any selector), sized by the page; its CSS pixels are logical pixels, the device's pixel ratio the scale |
| Frames | the page's `requestAnimationFrame` (Emscripten's main loop); a frame only when one is wanted (D-70: input, a due timer, an animation, a lease's rate) |
| Input | pointer, touch, wheel, keys (by `KeyboardEvent.code`; text from the key), and the Gamepad API's standard pads through `GamepadReader` (spec §6.1); only the person's own events (`isTrusted`): a script's are not input (openas §2.7) |
| The person's settings | read from the page when the host starts and again whenever one changes while it is open (the ninth kind of page event): `prefers-color-scheme` is `Runtime::dark_preferred` — a theme that follows it (omui.theme's schemes, `theme = ["system"]`; an application's `dark_theme`) turns with the system, no reload; `prefers-reduced-motion` and `prefers-reduced-transparency` are the runtime's reduced motion and transparency (or with what `WebOptions` asks). A Web package may name the page's colour for a dark preference (`background_dark`), so the page is dark before the program draws |
| A press before the program | a page may take its program later (dist-toy's `load`: `idle`, `interaction`, D-260); the press that made it take it — the node of the element pressed, kept by the page — is answered once, as an activation, when the program has presented its first frame and the node is in the layer (`omuiLoad.takePress`) |
| Input methods | a hidden textarea at the caret (`runtime.text_input_rect()`), named for the field: its composition is openmui's `PreeditEvent`, its result a `TextEvent`; a phone keyboard's letters and edits come the same way |
| Clipboard | as the page last knew it — a paste, or what the program copied — written through the Async Clipboard API; read again on return to the page where the person allowed it. The paste chord is the program's key; a paste by a menu is text |
| Document layer | after each batch of changes, the semantic tree as elements over the canvas (`DomMirror`; design 2026-11-26-web-document-layer-design.md, D-286): each node is the HTML element the static projection writes for it — `omui.semantic`'s `html_element`, one mapping for the page before the program and the layer over it — a heading an `h2`, a link an `a href`, a paragraph its text with the elements its marks make and its links standing in the sentence; what a person operates (a button, a checkbox) and what the canvas alone shows (a picture) keep their ARIA form. The layer's text is transparent, the canvas draws what is seen; it is text all the same, so the browser's find-in-page finds it and a selection takes it. The page's own projection is taken over element by element (`data-omui-target`), not rebuilt. Only what changed is sent. A status is said in a polite live region, an alert dialog in an assertive one. A screen reader's focus and activation reach the program. The pointer passes through to the canvas, except on a link: an `a href` takes it, so the browser shows where it goes, opens it in a new tab on a press with a modifier or the middle button and offers its menu — presses the program does not take — while a plain press, the hover and the wheel on it are the canvas's as before (the layer lies box on box over the canvas, and the program hears the layer's pointer events as the canvas's); the click after a plain press is not a second activation. Dragging over text selects with the program's own selection, as on every platform; the page's selection is what the browser starts itself (find, select all, the keyboard, a reader). A text the canvas draws line by line stands line by line: each line's words in the line's box (`omui.view`'s `text_lines`), fitted to the drawn width by the letters' size and then a stretch, so a match found or a selection made is where the canvas shows the words; a link that goes on into the next line is itself on its first line and an element like it on the next; white space a run begins with (a line of code's indentation) is a piece of its own, and a line that is only its break (an empty line of code) has none. Where the reader is in the page's own document before the program — a view scrolled there — the program asks before its first frame (`page::reading`) and draws that frame at the same place; an element the layer takes loses the look the page gave it. A view that scrolls scrolls on the page too (`scroll`: its offset and how far it goes; its children's boxes are places in what it scrolls, so scrolling sends one entry): the element follows the view frame by frame, and where the browser scrolls it — find-in-page bringing a match into view, a reader moving to an element — the page says so (`scroll`, the eighth kind of page event) and the view goes there; what only clips is put back if a browser scrolls it. Printed, the page is its document (`@media print`): the canvas steps aside and the layer's elements flow in the page — headings, paragraphs whose lines are one paragraph again at the paper's width, lists, code, links underlined — over as many sheets as it takes; what the canvas alone shows (a picture, a chart) is not printed. A picture the page's own document holds is the program's too (`page::picture`): its pixels as the browser decoded them, by its source as written, asked before any file — a picture that did not load fails saying so, one the document does not hold is a file's as before |
| `window.omui` | `snapshot`, `find`, `act`, `invoke`, `wait`, `capture`, `describe`, `changes` and `request` for the page's agents — `openmui.tree@0.1` as the desktop's agents speak it, served between frames. Promises of `{exitCode, data, errors, records}`. They come over the bus: openas decides (S-11), and the person's own actions are refused to them, whatever actor they name (S-10) |
| Store | the program's `Store` (`omui.native.store`: `omui::store()` in C++, `openmui:native/store` in scripts) is the page's `localStorage`, under `omui/<app>/` |
| Drawing | `WebOptions::renderer`, as the desktop's host decides it (§6.3): the page's WebGPU, else its WebGL2 (`openmui.rhi.webgpu`, `openmui.rhi.webgl`), each frame drawn whole by the GPU renderer into a texture the canvas's size and presented; else the CPU renderer, only the rows that changed, put on the canvas (`putImageData`). `automatic` takes a GPU only when it is hardware (a software adapter is slower than the CPU renderer: it is asked first without the canvas, which keeps no context of a device that will not draw); `gpu`, `webgpu` and `webgl` insist. `renderer()` says who draws; `read_frame()` reads back what was shown. With `present = false` a GPU's frames are drawn on a device made without the canvas and kept, not shown (a capture; a page whose GPU cannot show them). The CPU's pixels are the reference, the same bytes everywhere; a GPU's are within the renderer's tolerances of them |
| Fonts | `/fonts` in the page's file system, filled by the page before `main` (`dist-toy` writes the loader) |
| Visibility | the page hidden is the window's focus lost; `on_visibility` for the application (Toy's lifecycle) |

`omui::run` uses it on the Web: the umbrella package's `openmui:host`
partition and `src/host/web/shown.cpp` are chosen by the manifest for
`wasm32-emscripten`, so any openmui application builds for the browser
unchanged. The page's loop takes over and `main` does not return.

```cpp
import omui.host.web;

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
an agent through `window.omui`); `omui-devtools check web` runs both.

`tests/drawing_test.cpp`: without a page the CPU draws; in Chromium (`omui-devtools check browser`, with WebGPU, then WebGL2, the fonts preloaded) that device draws a page of boxes, borders, text in two scripts and a picture within the renderer's tolerances of the CPU's frame (4 levels at most, 64 pixels of 76 800 off by more than one: the rounded corners). Headless Chromium's SwiftShader differs by build and machine: some lose a WebGPU device that presents to a canvas unless their Vulkan is SwiftShader's too (under which ANGLE's WebGL2 reads back wrong), others offer no WebGPU adapter with those switches; WebGPU's run takes the default switches, else the Vulkan ones, else draws offscreen (`--offscreen`: `present = false`) and says so — CI's Chromium can present with neither, so there its WebGPU frames are compared offscreen and WebGL2's are presented.

Not yet: IndexedDB for what is
larger than localStorage holds; the clipboard's other kinds than text.

| Package | |
|---|---|
| [toy](toy) | Bilibili Toy's SDK (`window.toy`) as a thin optional layer: lifecycle, cloud storage, leaderboards |
