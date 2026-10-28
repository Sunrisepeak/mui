# What sets openmui apart

In order of priority. Each entry says what it is, why other frameworks lack it or have only part of it, what in the repository proves it (tests, benchmarks, works), and what is still missing. Only what the code and CI show is claimed.

Compared with today's major UI frameworks: Qt (Widgets / QML), Flutter, SwiftUI, Jetpack Compose, React / React Native / Electron, .NET (WinUI / MAUI / Avalonia), Slint, Dear ImGui / egui.

---

## 1. One semantic tree: pixels, accessibility, agents and tests read the same tree

**What**: an interface is first a semantic tree — each node a role, a name, a value, states and the actions it offers — and pixels are one rendering of it. Accessibility, agents and test assertions read the same tree.

**Elsewhere**: semantics (the accessibility tree) is mostly derived from the widget tree after the fact; tests use screenshots or selectors; agents use screenshots and coordinates.

**Evidence**: the semantic tree and its queries, compact snapshots, four headless levels H0–H3; `omui agent`; node ids that never depend on displayed text (S-9).

## 2. Agent-native, and safe

**What**: every application has an agent interface (`openmui.tree@0.1`) and an MCP server (`omui mcp`): Claude Code and other agents read the tree, find nodes, act and wait for the interface to settle. Actions have effect tiers: irreversible and external ones are confirmed by a person (S-1); an action marked `person` is the person's own — only they perform it, at the machine, and no agent can pass as them (S-10).

**Elsewhere**: no built-in agent interface; screenshots and clicks, or accessibility-API scripting, with no effect tiers and nothing stopping an agent from approving its own request.

**Evidence**: `tools/mcp`'s tests and a real-process end-to-end run in CI; confirmation by MCP elicitation; S-10's protocol tests (a request claiming to be the person is refused).

## 3. Determinism: three platforms, two compilers, the same bytes

**What**: the same input gives byte-identical pixels on Linux, macOS and Windows, with GCC and LLVM; animation is a pure function of time, so any frame can be computed directly; sessions replay exactly in headless mode.

**Elsewhere**: pixels depend on system fonts, system rasterisers and GPU drivers; tests tolerate differences or screenshot one platform.

**Evidence**: pinned pixel hashes on all six CI rows (examples, components, showcases); a CPU reference rasteriser without floating-point contraction; text and i18n that use no system libraries (no ICU).

## 4. One tree, four languages, and view documents

**What**: C++, TypeScript, Python, Lua and `*.view.omui.json` build the same semantic tree; components are contract-first, and the bindings of the four languages are generated.

**Evidence**: CTS scenarios record byte-identical traces in the three script languages; `omui-gen` generates bindings, stubs and schemas.

## 5. Strong animation: every visual property, down to glyphs and pixels

**What**: an independent animation engine (`openmui.anim`) whose clock can be injected, paused, stepped and reversed; any visual property transitions; effect parameters are animatable properties too.

## 6. Rendering: a CPU reference and a GPU path; effects first-class

**What**: one renderer. The CPU backend is the deterministic reference; the GPU backend runs on `omui.rhi` within stated tolerances of it. Custom effects are Slang (a reload in about 50 ms); backdrop effects such as liquid glass are a general capability; style packs change the look and leave the semantic tree as it is.

**Evidence**: GPU tests against the CPU reference, scene by scene; the first benchmark report — on an RTX 4080 the five style packs take 0.24–0.64 ms a frame, ten liquid-glass panels 0.82 ms.

## 7. Performance that is proven: budgets in CI, a public report

**Evidence**: 36 budgets in `tests/bench`, checked on every change; `docs/en/benchmarks/`; S1's 1000-window zoom sweep at a frame p99 of 5.7 ms. Frame demand and pacing (ProMotion / VRR) are exposed in layers.

## 8. Simple to write, deep to control (progressive disclosure)

**What**: each capability has four levels — L0 right by default (incremental frames, caches, parallel raster, pacing are automatic); L1 hints as properties; L2 explicit control (render to texture, pipeline stages, parallelism); L3 the layer beneath (the rhi device, custom passes, external textures, frame-demand leases). The performance-critical parts are C++ modules, so a developer can go all the way down to the hardware when they need to.

## 9. Everything replaceable, each piece releasable alone

**What**: geometry, animation, layout, text, raster, render, rhi, i18n, recording and tracing are independent components with no dependency on the rest; a frame's pipeline is orchestrated from plugins; hosts are plugins too (SDL3 is the default desktop host, not a dependency of the framework).

## 10. Internationalisation without ICU, the same on every platform

**What**: the i18n engine and its data are separate, and the engine can be released alone; the data is generated from CLDR; messages are MessageFormat 2; English and Chinese are first-class; a language change updates text in place with the same ids.

Chinese typography follows CSS Text 4: autospace between Chinese and Latin text, fullwidth punctuation set solid in pairs and at line starts, no closing mark starting a line.

**Evidence**: `modules/i18n`'s tests agree with CLDR's results; `tr()` means the same in every way of writing a view; `openmui.text`'s Chinese typography tests.

---

## Side by side

| | openmui | Qt | Flutter | SwiftUI | Compose | React / Electron | Dear ImGui |
|---|---|---|---|---|---|---|---|
| Semantic tree as the primary model | ✅ | partly (a11y) | partly (Semantics) | partly | partly | partly (ARIA) | ✗ |
| Built-in agent interface / MCP | ✅ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Effect tiers, a person's confirmation | ✅ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Byte-identical pixels across platforms | ✅ | ✗ | close (Skia/Impeller; system fonts) | ✗ | ✗ | ✗ | ✗ |
| Exact record and replay | ✅ | ✗ | ✗ | ✗ | ✗ | partly (tools) | ✗ |
| Several languages, one tree | ✅ (4 + documents) | C++ / QML / Python | Dart | Swift | Kotlin | JS | C++ |
| Custom shader effects | ✅ (Slang, hot reload) | ✅ (QML ShaderEffect) | ✅ (FragmentProgram) | ✅ (Metal) | ✅ (AGSL) | partly (WebGL) | by hand |
| Performance budgets in CI, a public report | ✅ | ✗ | partly | ✗ | partly (Macrobenchmark) | ✗ | ✗ |
| Components releasable alone | ✅ | partly | ✗ | ✗ | partly | ✅ (npm) | — |

*"✗" means not built in, not impossible.*
