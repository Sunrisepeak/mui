# omui.sdk

The SDK's umbrella: `import omui;` gives an application the runtime (signals, views, immediate mode, semantics, the agent endpoint), expressions and view documents, the base components, the hosts and `omui::run`, which shows an application on the desktop, in the page or on Android, or runs it headless for tests and agents ([src/openmui.cppm](src/openmui.cppm)).

| Part | Label | |
|---|---|---|
| the umbrella, `omui::run`, the development host's transport | stable | re-exports omui's packages (`reexports` in the manifest) |
| runtime generation (`src/generate/`, module `omui.generate`) | preview | below |
| Android's host | preview | `lower` in the manifest |
| the templates (`templates/`: app-ts, app-cpp, component, native, theme) | stable | `omui new NAME [--template T]` is `mcpp new NAME --template omui.sdk:T`: this package's archive carries them |

What an application carries is switched by this package's features (B6), each on by default: `accessibility-bridge` — the platform's service through AccessKit, or `no-accessibility-bridge`, the mapping alone (example 01 on Linux: 18.0 MB → 16.5 MB loaded). An application chooses with `omui.sdk = { …, default-features = false, features = ["no-accessibility-bridge"] }`.

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
