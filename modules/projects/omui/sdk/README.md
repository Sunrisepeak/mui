# omui.sdk

The SDK's umbrella: `import omui;` gives an application the runtime (signals, views, immediate mode, semantics, the agent endpoint), expressions and view documents, the base components, the hosts and `omui::run`, which shows an application on the desktop, in the page or on Android, or runs it headless for tests and agents ([src/openmui.cppm](src/openmui.cppm)).

| Part | Label | |
|---|---|---|
| the umbrella, `omui::run`, the development host's transport | stable | re-exports omui's packages (`reexports` in the manifest) |
| runtime generation (`src/generate/`, module `omui.generate`) | preview | below |
| Android's host | preview | `lower` in the manifest |
| the templates (`templates/`: app-ts, app-cpp, component, native, theme) | stable | `omui new NAME [--template T]` is `mcpp new NAME --template omui.sdk:T`: this package's archive carries them |

What an application carries is its **world**, a feature of this package ([`mlibs.compose`](../../../base/compose), D-220): `full` (the default: everything), `app`, and `lean` (the core and pixels). An application writes nothing and has `full`, or one line — `omui.sdk = { …, default-features = false, features = ["app"] }` — or its own choice of capabilities (`features = ["lean", "accessibility-bridge"]`); `omui dev` builds a C++ application with `full` whatever the line says. Each capability is one additive feature a world implies, and the build program writes the unit that installs its provider (`build.mcpp`, through `mlibs.build.compose`); one not installed is not in the program. Today's capabilities: `gpu` — the GPU path of a desktop window ([`omui.host.sdl3.gpu`](../host/sdl3/gpu): SDL_GPU's device and xrender's renderer), in `app` and `full`; without it the CPU renderer draws every frame, as on a machine without a GPU, and effect files are not loaded; `accessibility-bridge` — the platform's accessibility service ([`omui.accessibility.bridge`](../accessibility/bridge): AT-SPI, NSAccessibility, UI Automation through AccessKit), in `app` and `full`; without it accessibility is not served, as on a platform without a service. Example 01 loads 18,080 KiB as `full` or `app` and 15,175 KiB as `lean`. The rest are off by default: `no-gamepad-hidapi` leaves out SDL's HIDAPI game-controller drivers on the desktop, the platform's own joystick input staying (example 01: 246,917 B less; `omui.sdk = { …, features = ["no-gamepad-hidapi"] }`).

## Agent surface

The packages it re-exports are reached as each says; runtime generation through [omui.agent](../agent)'s `generate.*` catalog.

## Runtime generation (`omui.generate`, preview)

Runtime generation as an application ships (D-144, convergence plan W5): interface an Agent makes while the application runs, put into the surfaces the application declares.

A draft goes through stages, each asked for and each readable — draft → validated → prepared → previewed → committed. A commit is a version of its surface that undo takes back; versions are kept on disk and checked again when the application starts. How open it is (off, data, limited, all) is the developer's in `omui.config.ts`, narrowed by the user's settings and the session (openas). `:a2ui` reads A2UI's server messages as one input format: a stream's surfaces drafted and committed as they come.

Its operations are the `generate.*` catalog, [omui.agent's generate.ops.xdef.ts](../agent/generate.ops.xdef.ts) (the catalogs an application's endpoint describes live there, beside the protocol's and Live Design's); `Generation` implements them on an application's runtime.

Part of the SDK (`src/generate/`, module `omui.generate`), not of [omui.agent](../agent): generation takes view documents ([omui.viewdoc](../viewdoc)); the endpoint takes none. Held together they made the endpoint depend on documents and documents' tests on the endpoint (red list 44).

```sh
mcpp test -p omui.sdk generation_test
```
