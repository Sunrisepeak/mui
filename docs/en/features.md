# What sets omui apart

In order of priority. Each entry says what it is, why other frameworks lack it or have only part of it, what in the repository proves it (tests, benchmarks, works), and what is still missing. Only what the code and CI show is claimed.

Compared with today's major UI frameworks: Qt (Widgets / QML), Flutter, SwiftUI, Jetpack Compose, React / React Native / Electron, .NET (WinUI / MAUI / Avalonia), Slint, Dear ImGui / egui.

---

## 1. One semantic tree: pixels, accessibility, agents and tests read the same tree

**What**: an interface is first a semantic tree — each node a role, a name, a value, states and the actions it offers — and pixels are one rendering of it. Accessibility, agents and test assertions read the same tree.

**Elsewhere**: semantics (the accessibility tree) is mostly derived from the widget tree after the fact; tests use screenshots or selectors; agents use screenshots and coordinates.

**Evidence**: the semantic tree and its queries, compact snapshots, four headless levels H0–H3; `omui agent`; node ids that never depend on displayed text (S-9).

## 2. Agent-native, and safe

**What**: every application has an agent interface (`openmui.tree@0.1`) and an MCP server (`omui mcp`): Claude Code and other agents read the tree, find nodes, act and wait for the interface to settle. Actions have effect tiers: irreversible and external ones are confirmed by a person (S-1); an action marked `person` is the person's own — only they perform it, at the machine, and no agent can pass as them (S-10). The developer sets the application's cap (`AppSpec::agent`, fixed when it is built); the person grants an agent an effect tier over a scope of verbs, through the application's own interface, and what a grant covers needs no confirmation (S-11). What is reversible can be taken back: components say how (a switch toggles back, a field gets its text back), the window keeps the history and `undo` / `redo` are actions an agent sees and performs; several actions can be one transaction — all done or all taken back; and an agent can ask what an action would do before doing it (a dry run changes nothing) (S-12, S-13). The rules are the open standard [openas](https://github.com/openlifa/openas) §5's, decided by `openas::decide`.

**Elsewhere**: no built-in agent interface; screenshots and clicks, or accessibility-API scripting, with no effect tiers and nothing stopping an agent from approving its own request.

**Evidence**: `tools/mcp`'s tests and a real-process end-to-end run in CI; confirmation by MCP elicitation; S-10's protocol tests (a request claiming to be the person is refused); S-11's (beyond the cap refused, an agent in a grant's chain not asked again, any other agent asked as before).

## 3. Determinism: three platforms, two compilers, the same bytes

**What**: the same input gives byte-identical pixels on Linux, macOS and Windows, with GCC and LLVM; animation is a pure function of time, so any frame can be computed directly; sessions replay exactly in headless mode.

**Elsewhere**: pixels depend on system fonts, system rasterisers and GPU drivers; tests tolerate differences or screenshot one platform.

**Evidence**: pinned pixel hashes on all six CI rows (examples, components, showcases); a CPU reference rasteriser without floating-point contraction; text and i18n that use no system libraries (no ICU).

## 4. One tree, four languages, and view documents

**What**: C++, TypeScript, Python, Lua and `*.view.omui.json` build the same semantic tree; components are contract-first, and the bindings of the four languages are generated.

**Evidence**: CTS scenarios record byte-identical traces in the three script languages; `omui-gen` generates bindings, stubs and schemas.

## 5. Independent animation, composable playback

**Available**: `import hanim;` exposes objects, reusable strict contour morphs,
ordered effect parameters, sequence/parallel/stagger and an explicit-clock
Engine. A leaf such as `hanim::anim::morph(source, target).duration(600ms)` can
be passed to `hengine.play(animation)` or prepared once with
`hengine.prepare(animation)` and replayed. Live refs capture at preparation;
explicit snapshots stay frozen. The same player supports seek, pause, reverse
and current-pose retargeting without changing the application's business state.

Common property recipes include `move_to`, `move_by`, `rotate_to`, `scale_to`
and `fill_to`. `anim::to(object, PropertySet{...})` selects several parts and
properties with one timing. Separate leaves can animate independent channels
of the same object with different timings. Here `card` is an HObject with a
part whose stable id is `icon`:

```cpp
auto animation = hanim::composite(hanim::parallel(
    hanim::anim::move_to(card, {120, 40}).duration(600ms),
    hanim::anim::to(card, {.rotation = std::numbers::pi, .part = "icon"})
        .duration(250ms).easing(hanim::ease::out_quad)))
    .with_effect(hanim::fx::blur(0, 2));
auto playback = hengine.play(animation);
```

The object is drawn once. Unselected local properties retain their values;
parent transforms still apply. Overlapping writes to the same channel are
reported as conflicts. Shared image effects wrap the parallel expression;
different image scopes are not silently merged. Native tools and Agents can
inspect the retained author graph and channel timing through `hengine.plan`.

Typed `fx::vertex_path(PointPath::bend(0.25))` and `fx::translation_path(...)`
modify matched geometry or local positions before rendering. They can be
combined with blur/tone, preserve endpoints, and share the core point operator
with the C++ UI path adapter. Repeated path modifiers add displacement in the
same endpoint frame; they do not change matching rules.

UI motion exposes the same operator through ordinary configuration:

```cpp
auto motion = Value{}.set("use", "morph")
    .set("vertex_path", Value{}.set("bend", 0.22));
drawing.prop("motion", Source(motion));
```

Components draw states; the runtime animates matched fill/stroke paths. The
basic-motion page opens with a triangle-to-circle example; Details reveals
path, timing and local style controls, plus reusable motion JSON. A bend is a
fraction of each point's displacement. It does not change text, image geometry
or hit contours. Reduced transitions omit this displacement.

**Evidence**: an independent public consumer tests geometry and controls in
dev/release; a separate consumer uses the shared CPU renderer for actual pixels
and region exports. Rendering is opt-in; the base `hanim.core` stays independent.

**Next**: additive/weighted channel mixing, world-space constraints, shared
geometry conversions, 3D/image/rig domains and configurable UI integration.
Nested image scopes and independent 2D property channels are available; current UI motion primarily uses core primitives;
the website's scene count is not a count of public engine recipes.

## 6. Rendering: a CPU reference and a GPU path; effects first-class

**What**: one renderer. The CPU backend is the deterministic reference; the GPU backend runs on `mlibs.rhi` within stated tolerances of it. Custom effects are Slang (a reload in about 50 ms); backdrop effects such as liquid glass are a general capability; style packs change the look and leave the semantic tree as it is.

**Evidence**: GPU tests against the CPU reference, scene by scene; the benchmark report (`docs/en/benchmarks/`, 2026-10-27) — on an RTX 4080 the five style packs take 0.36–1.38 ms a frame at p95 (the runtime's frame and the GPU's, submitted and waited for), ten liquid-glass panels at 1080p 1.61 ms.

## 7. Performance that is proven: budgets in CI, a public report

**Evidence**: 48 budgets in `tests/bench` (`budgets.bench.omui.json`), checked on every change; `docs/en/benchmarks/`; S1's 1000-window zoom sweep drawn by the GPU at a frame p99 of 7.11 ms (p95 1.20 ms; on the CPU path p95 0.115 ms). Frame demand and pacing (ProMotion / VRR) are exposed in layers.

## 8. Simple to write, deep to control (progressive disclosure)

**What**: each capability has four levels — L0 right by default (incremental frames, caches, parallel raster, pacing are automatic); L1 hints as properties; L2 explicit control (render to texture, pipeline stages, parallelism); L3 the layer beneath (the rhi device, custom passes, external textures, frame-demand leases). The performance-critical parts are C++ modules, so a developer can go all the way down to the hardware when they need to.

## 9. Everything replaceable, each piece releasable alone

**What**: geometry, animation, layout, text, raster, render, rhi, i18n, recording and tracing are independent components with no dependency on the rest; a frame's pipeline is orchestrated from plugins; hosts are plugins too (SDL3 is the default desktop host, not a dependency of the framework).

## 10. Internationalisation without ICU, the same on every platform

**What**: the i18n engine and its data are separate, and the engine can be released alone; the data is generated from CLDR; messages are MessageFormat 2; English and Chinese are first-class; a language change updates text in place with the same ids.

Chinese typography follows CSS Text 4: autospace between Chinese and Latin text, fullwidth punctuation set solid in pairs and at line starts, no closing mark starting a line.

**Evidence**: `modules/content/i18n`'s tests agree with CLDR's results; `tr()` means the same in every way of writing a view; `openmui.text`'s Chinese typography tests.

## 11. Windows: the system's by default, the framework's own in depth

**What**: a window has the system's frame unless the application asks for its own (`AppSpec::window = "framework"`, or `--window framework`; `switchable` lets the person choose, through the verb `window.frame`). The framework's frame (`openmui.ui.window`) is a component like any other: shadow, rounded edge, title bar, buttons in two looks, following the window's state; its title bar moves the window and its edges resize it through the system, so snapping still works. Every window operation is a verb (`window.minimize`, `window.toggle_maximize`, `window.fullscreen`, `window.close`), the same for a click, a shortcut and an agent. Closing is immediate: the window takes no more input, which goes to what is beneath it, while it dissolves.

**Elsewhere**: custom title bars are per-platform work (Electron's `-webkit-app-region`, Flutter's window_manager plugins, WinUI's title bar customisation), rarely consistent across platforms, and none of them makes the window's operations part of an agent-readable model.

**Evidence**: `components/window`'s tests (both looks pinned to the byte, the parts' hit areas, maximized and inactive states, buttons named in en / zh-Hans / zh-Hant, the application's paths unchanged inside the frame); `modules/ui/host/sdl3`'s tests (the verbs, switching, the close that dissolves and takes no input).

---

## Side by side

| | omui | Qt | Flutter | SwiftUI | Compose | React / Electron | Dear ImGui |
|---|---|---|---|---|---|---|---|
| Semantic tree as the primary model | ✅ | partly (a11y) | partly (Semantics) | partly | partly | partly (ARIA) | ✗ |
| Built-in agent interface / MCP | ✅ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Effect tiers, a person's confirmation | ✅ | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Agent grants under a developer's cap (an open standard) | ✅ (openas) | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |
| Byte-identical pixels across platforms | ✅ | ✗ | close (Skia/Impeller; system fonts) | ✗ | ✗ | ✗ | ✗ |
| Exact record and replay | ✅ | ✗ | ✗ | ✗ | ✗ | partly (tools) | ✗ |
| Several languages, one tree | ✅ (4 + documents) | C++ / QML / Python | Dart | Swift | Kotlin | JS | C++ |
| Custom shader effects | ✅ (Slang, hot reload) | ✅ (QML ShaderEffect) | ✅ (FragmentProgram) | ✅ (Metal) | ✅ (AGSL) | partly (WebGL) | by hand |
| Performance budgets in CI, a public report | ✅ | ✗ | partly | ✗ | partly (Macrobenchmark) | ✗ | ✗ |
| Components releasable alone | ✅ | partly | ✗ | ✗ | partly | ✅ (npm) | — |

*"✗" means not built in, not impossible.*
