# Subject: ui-framework — openmui as a UI framework

**What:** the framework an application is written with: the runtime and its frame pipeline (`modules/ui/view`), signals and builders (`modules/ui/core`), the semantic tree and agent interface (`modules/ui/semantic`, `modules/ui/agent`, openas), components (`components/`), contracts and generated bindings (`spec/`, `mcpp/contracts`, `tools/gen`), hosts (`modules/ui/host/*`: headless, SDL3 desktop, web), language bindings (`lang/`), tools (`tools/`: omui CLI, inspector, mcp, lint). Read AGENTS.md, `.agents/docs/README.md` and the architecture and implementation-plan documents, `docs/en/features.md`, the READMEs, `tests/bench` and its budgets, `.github/workflows`; sample real code in every layer.

**Top 3:** Flutter · Qt 6 (Qt Quick / QML) · Jetpack Compose (with Compose Multiplatform). **Reference on some dimensions:** SwiftUI, Slint, egui, React Native.

**Dimensions** (key — what is judged):
- `architecture` — the semantic tree as the core; frame pipeline and stage plugins; layering; how rendering, input, accessibility and agents read one tree; failure handling.
- `app_api` — writing an application: builders, signals, regions, state, navigation, async; how little code a real screen takes; errors a developer meets.
- `component_model` — writing a component: contracts, generated bindings, theming, the public-API-only rule; versus Flutter's widgets, QML components, composables.
- `component_library` — breadth and depth of the shipped components (inputs, lists, menus, dialogs, text, canvas, markdown, timeline, windows) against each top-3's standard library.
- `layout_text` — layout engine, text shaping, bidi, CJK, IME, rich text, fonts.
- `accessibility` — semantic tree, native screen-reader adapters, keyboard, focus, contrast; tested with real assistive technology?
- `agent_native` — agents reading and acting through the same actions with openas checks, undo, transactions, MCP; how real and how differentiated (no top-3 has it: judge it against the best agent-automation of each — Flutter's semantics + integration_test, Qt's accessibility + Squish, Compose's semantics + UI tests).
- `determinism_testing` — headless levels, pinned pixels across platforms, replay, the testing story versus golden tests in Flutter / Compose screenshot tests / Qt Test.
- `performance` — frame cost, idle cost, allocation, startup, memory, GPU path, with benchmarks and budgets; versus published numbers of the top 3.
- `platforms_web` — desktop OSes, the Web (wasm, WebGPU, WebGL2), mobile; what is proven in CI.
- `tooling_dx` — build plugins, CLI, inspector, hot reload, diagnostics, templates, docs for developers.
- `code_quality` — .cppm/.cpp split, modules, no exceptions/RTTI in core, allocation discipline, readability; directory tree and package split.
- `docs_spec` — the open specification, conformance tests, guides, API reference.

**Images:** none required (code review); attach the site's lens shots or tool screenshots when they exist.
