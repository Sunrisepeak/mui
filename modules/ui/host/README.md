# modules/ui/host

Hosts run a runtime: they bring the clock, the input and the surface (D-77: a host is a plugin; nothing below depends on one).

| Package | |
|---|---|
| [headless](headless) | A stepped virtual clock, injected input, CPU pixels and PNG, the agent endpoint: tests, agents and video |
| [sdl3](sdl3) | The desktop over SDL3: windows (system- or framework-drawn), input, IME, clipboard, gamepads (SDL's mapping database), GPU or CPU frames, effect reloading |
| [web](web) | The browser (wasm32-emscripten, one thread): a canvas, requestAnimationFrame, pointer, touch, keys and gamepads, input methods in a hidden textarea, the clipboard, the semantic tree as a hidden DOM, `window.omui` for the page's agents, the store on localStorage, CPU frames on the canvas; Toy's SDK in [web/toy](web/toy) |
