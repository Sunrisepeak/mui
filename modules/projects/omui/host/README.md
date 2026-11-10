# modules/ui/host

Hosts run a runtime: they bring the clock, the input and the surface (D-77: a host is a plugin; nothing below depends on one).

| Package | |
|---|---|
| [android](android) | Android (W10): the NativeActivity's window drawn by the framework, touches and keys (Back is Escape), the display's density as the scale, pausing and resuming; the application is the library the platform loads (an APK by dist-apk, no Java), its default fonts unpacked from the APK's assets |
| [headless](headless) | A stepped virtual clock, injected input, CPU pixels and PNG, the agent endpoint: tests, agents and video |
| [sdl3](sdl3) | The desktop over SDL3: windows (system- or framework-drawn), input, IME, clipboard, gamepads (SDL's mapping database), GPU or CPU frames, effect reloading |
| [web](web) | The browser (wasm32-emscripten, one thread): a canvas, requestAnimationFrame, pointer, touch, keys and gamepads, input methods in a hidden textarea, the clipboard, the semantic tree as a hidden DOM, `window.omui` for the page's agents, the store on localStorage, CPU frames on the canvas; Toy's SDK in [web/toy](web/toy) |

Web is a priority target. Platform SDKs such as bilibili Toy are Web
sub-backends: reuse the common runtime/rendering path and adapt only the
platform's bridge, permissions, lifecycle and delivery constraints. A host,
graphics/compute/audio device backend and package/output target are separate
choices; adding one must not fork upper-layer application or engine logic.

The [multi-engine architecture](../../../../.agents/docs/2026-11-04-multi-engine-product-architecture-design.md#5-跨平台设备与宿主能力)
defines the staged expansion: validate new public capabilities with a reference
platform, then implement additional backends against shared and target-specific
CTS. TUI is a planned terminal host/cell-rendering target, distinct from a CLI;
it should reuse state, semantics and actions with explicit capability-based
presentation. It is not an implemented host in the table above.
