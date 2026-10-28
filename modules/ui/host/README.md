# modules/ui/host

Hosts run a runtime: they bring the clock, the input and the surface (D-77: a host is a plugin; nothing below depends on one).

| Package | |
|---|---|
| [headless](headless) | A stepped virtual clock, injected input, CPU pixels and PNG, the agent endpoint: tests, agents and video |
| [sdl3](sdl3) | The desktop over SDL3: windows (system- or framework-drawn), input, IME, clipboard, GPU or CPU frames, effect reloading |
