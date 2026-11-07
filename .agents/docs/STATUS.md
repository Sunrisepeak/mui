# STATUS

The single progress entry (plan §12, at most 200 lines). Updated by every checkpoint commit on PR #1 (`openmui-design`). Decisions and the full roadmap: [top-level convergence plan](2026-11-06-top-level-assessment-convergence-design.md).

## Current milestone: M0 — stop the bleeding

Goal: the whole CI matrix green once, Pages deployed. CI does not block progress, but every round starts by reconciling the previous checkpoint's CI (plan §10.1).

| Item | State |
|---|---|
| Convergence plan, AGENTS.md rewrite, STATUS | done (this checkpoint) |
| `mcpp.lock` untracked and ignored; `site.sh` without `--locked` | done |
| CI red list below | in progress |
| CI: a run in progress is not cancelled by a newer push (the x64 rows need ~1 h 50 min; 75655b7's were cancelled minutes before finishing) | done |

## M1 — structure and naming: done locally (exit: lint 0, every member builds and passes), CI to confirm

| Step | State |
|---|---|
| ① lint path→layer table; `mlibs.utils`; fuzzy into its consumer | this checkpoint (D-101): layers by name from a path-prefix table holding today's and M1's directories; hash, log, trace, testing → `modules/base/utils`, every tool built (a feature per tool recompiled the graph per member selection, measured); lint `testing` (test checks only in `tests/`) and `closure` (render and simulation closures, from the retired `tools/ci/foundation-migration`); `mlibs.fuzzy` → `openmui.native.fuzzy:matching` |
| ② hanim and xrender into `modules/projects/` | this checkpoint (D-102): `modules/projects/hanim` (hanim, hanim.core) and `modules/projects/xrender` (graph, vector, effect, raster, cts, rhi/{sdl,webgpu,webgl}); identities, modules and namespaces `xengine.render*`, `mlibs.raster`, `mlibs.rhi.{sdl,webgpu,webgl}` → `xrender.*`; the render graph is `xrender.graph` (a bare `xrender` collides with X11's `compat.xrender` in mcpp, below); legacy `openmui.*` forwarders kept until ⑤; READMEs: one-sentence definitions and candidate signature features |
| ②b `mlibs.text` without the renderer | this checkpoint (D-103): text's own `FontRef`/`Glyph`, glyphs as geometry (`outline`, `color_layers`); new `xrender.text` (layer content): `GlyphCache` and `paint`; the display list fills glyphs in place; the pinned glyph pixels unchanged; Windows GPU: the ICD registered in HKLM (red 13) |
| ③ xengine and gongfu | this checkpoint (D-104): `modules/projects/xengine` (simulation, audio, video, timeline, world, input, physics, net, ui) and `modules/projects/gongfu`; `openmui.{audio,video,timeline}*`, `mgame.*` → `xengine.*`; `mgame.sim` forwarder removed; `mgame/`, `modules/media`, `modules/gfx/simulation` gone |
| ④ omui with components, lang, sdk, templates | this checkpoint (D-105): `modules/ui`, `components/`, `lang/`, `templates/` → `modules/projects/omui/`; the root package → `modules/projects/omui/sdk`; the root manifest is the workspace alone |
| ⑤a every forwarder removed, lint `legacy-import` | this checkpoint (D-106): the 31 `openmui.*`/`mgame` forwarder modules and their `omui::` aliases gone; 400+ files name the owners (`mlibs::`, `xrender::`, `xengine::`); a file that uses an owner's names unqualified says so in a local `using` block |
| ⑤b `openmui.*` → `omui.*` (packages, modules) | this checkpoint (D-107); kept: the specification (`openlifa.openmui`, `openmui.spec`), protocol and format ids, the C ABI, the script API (`import openmui`, `openmui:native/*`: the specification's stubs define it; rule: specification-level names openmui, implementation names omui) |

## CI red list

Baseline: `810d1fc` (last all-green CI: `26d1e9a`, 2026-11-01). Kind: R = regression, E = environment, F = flaky/time, T = test expectation.

| # | Failure | Where | Since | Kind | State |
|---|---|---|---|---|---|
| 1 | `WeightedPaint` incomplete type in `std::span` arithmetic | Windows build, omui pack | ≤ `c9b0d1c` | R | fixed, confirmed on Windows CI (75655b7: build and omui pack green): MSVC STL checks C++23 const-iterator concepts when `std::span<const T>` is instantiated, so a span of the then-incomplete `WeightedPaint` cannot be a `Paint` member; `mlibs::memory::View<T>` replaces it in raster and render |
| 2 | `hanim.render.vector.pcm` has no rule (view's `hanim` feature) | Web build (`host/web`, `site`) | ≤ `810d1fc` | E (mcpp) | worked around (Web row green on 00ca8ba, 68e61c9): the site declares its direct `hanim` features; local Web row 44 members, 106 passed. Upstream mcpp bug, see below |
| 3 | Metal: cellular `rules.slang` buffer index 0 reserved twice | macOS `rhi.sdl` | ≤ `ab4d823` | R | fixed, confirmed on macOS CI (68e61c9): Slang numbers Metal bindings by HLSL register without the space; `mlibs.shader` now rebinds MSL entry points as SDL_GPU binds Metal (also fixes the latent `greedy.slang` palette at buffer(2) instead of buffer(1)) |
| 4 | `viewdoc/slots are filled by code` (`viewdoc_test.cpp:378`) | all | ≤ `074e3bd` | R | fixed: the test predates default exit motions; replaced content is `removing` (S-4) until it settles |
| 5 | widget-breakout: room 3 hash, run flow, script reload (6 checks) | all | `074e3bd` (bisected) | R | fixed: an untargeted style motion became a `*` rule for every node (074e3bd); it no longer takes the window's root, and the game declares its live surfaces (root, field, canvas: no update morph). The site keeps its pinned look. Tests wait for cards that enter by the room's style; the choice frame re-pinned (15 px, at most 1/255) |
| 6 | promo-video pinned frames (`promo_test.cpp:129/133`) | all | `77979b5` (bisected) | R→baseline | re-pinned with the semantic change stated: 77979b5 makes motion level off skip a style's cross-fade (asserted by `view.theme_motion`); the film runs at off so frame 1860 (the switch to retro, 31.0 s) is now a cut and 1820 no longer carries the previous fade. Restoring the film's fades is M2 input (below) |
| 7 | style-gallery liquid tab lens stretch (`styles_test.cpp:143`) | all | `77979b5` (bisected) | R | fixed: `Matter::follow_layout` replaced every style-wrapper shape with the target's; only allocation carriers adopt now, state shapes (the lens) keep morphing |
| 8 | site motion/site tests exceed 300 s | Linux x64/arm | — | F | fixed in CI: the site has its own lane (`site`, release, 1200 s per test); the matrix rows exclude it. Local release: 22 passed |
| 9 | GCC dev/release jobs cancelled at the 150-minute limit | Linux GCC | — | F | 0dae888: both rows still in "Test every member" at 150 min (LLVM x64: green in ~1 h 50 min). Plan §10.1 ③ applied: the GCC rows, and the steps gated on them (consumers, templates, sweep, benchmarks, README media), run nightly and on demand (`ci.yml` schedule, `workflow_dispatch`, pushes to main); a pull request's matrix leaves them out. Next: make them fit (test-binary build time) |
| 10 | Pages: `--locked` resolution lacks accesskit | Pages | `12d2e15` | E | build and delivery budget fixed (4,886,971 B). Browser checks: CI's Chrome refuses clipboard reads, so the scripts observe the page's own clipboard writes (a refused read can no longer pass as `{}`), and NoScript navigation is awaited. Local Chromium: presentation 159/159, motion 151/151. Confirmed: Pages deployed from 75655b7 |
| 11 | cellular reaction kernel wrong on Direct3D 12 / DXBC (WARP): tick 1, index 515 (channel 2 of the one active cell) GPU 0, CPU 1 | Windows `rhi.sdl` | never validated (Windows did not build since 11-04) | R | open: CI's Windows row now runs lavapipe as intended (`SDL_GPU_DRIVER=vulkan`); the D3D12 path needs its own run. The rhi CTS passed on D3D12 |
| 12 | agent e2e: `delete-all` by an agent ran without asking | Linux (example 07) | 20e6f54 (development mode is full access) | T | fixed: the acceptance starts the example in dual-user mode, the needs-answer path it checks; the whole e2e passes locally |
| 13 | every GPU test on Windows: "SDL_HINT_GPU_DRIVER vulkan unsupported!" (lavapipe, 26 checks) | Windows | 75655b7 (first lavapipe run) | E | root cause (0dae888, loader diagnostics): the runner's processes are elevated and the Vulkan loader ignores `VK_DRIVER_FILES` there ("Registry lookup failed to get ICD manifest files"). Fixed, confirmed on Windows CI (19ff8c3: the row green): `tools/ci/gpu.sh` registers its manifest (absolute path) under `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` |
| 14 | Python suite hangs 240 s in `test_the_scenarios_record_their_traces` | macOS (intermittent) | ≤ 75655b7 | R | fixed, confirmed on macOS CI (0dae888): a handler on the script thread that called the runtime kept the GIL while it waited for the runtime's thread, which was waiting for the GIL to return from `act`. The binding now always releases the GIL around the session's work; a new test (200 acts whose handler invokes a verb) hangs on the old code on Linux and passes 3/3 with the fix |
| 15 | Web: the WebGPU and WebGL pages' node tests read `src/types.cppm` beside them (ENOENT) | Web | 19ff8c3 (M1 ②: the backends left `modules/gfx/rhi`) | R | fixed: they read mlibs.rhi's sources at `modules/gfx/rhi/src`; node: 18/18 |
| 16 | Web: "no WGSL or ESSL in the build's shader partitions" | Web (browser checks) | 09702a9 (M1 ④: the root declares no package) | R | fixed: `browser.sh` read `[package]` of the root manifest; it reads the members'. (The WebGPU drawing test failing on the default and Vulkan switches and passing offscreen is the script's designed fallback for this Chrome, as on 0dae888) |
| 17 | site: the pinned pages and the reference run's frames differ | site, Web | baaab7f (M1 ⑤b) | T | re-pinned with the semantic change stated: the site embeds `docs/` and its `content/`, whose package names became `omui.*`; Linux CI and this machine make the same new hashes (`d68fc52a5c531679`, first frame `fb4e67a4281c6311`); the reference table regenerated by the test itself; site 22/22 |
| 18 | Pages: "google-chrome did not open its DevTools port" (site-perf) | Pages | 1b3f5c72 | F | fixed (this checkpoint): `page.mjs` guessed a port in 9300–9799 and waited 10 s for it; Chromium now picks a free port (`--remote-debugging-port=0`) and says which in its profile's `DevToolsActivePort`, read for up to 60 s while the process lives (a crash is reported as one). `close()` is awaitable and takes the profile with it; `drive-page.mjs` uses the same helper. cb26e381 on Pages: the port opened, but removing the profile as Chromium exited raced its helpers (ENOTEMPTY, thrown from the exit handler): the removal retries and never throws |
| 19 | hanim API probe: "non-independent repository source: modules/projects/hanim/core/src/core.cppm" | Linux (hanim public authoring step) | M1 ③ (hanim moved to `modules/projects/`) | R | fixed (this checkpoint, found locally): the probe's independence check allowed `modules/base` and `modules/gfx` only; it allows the gfx-layer projects hanim and xrender too, and still refuses UI and game sources. Probe, play consumer and omui consumer pass locally |
| 20 | templates: `component` — "dependency 'omui.build.contracts': no package found for exact selector" | Linux arm (templates step; x64 Linux runs it too) | M1 (the build plugins became `omui.build.*`, 4a261c33) | R | fixed (this checkpoint): `tools/ci/templates.sh` still rewrote `openmui.build.<plugin>` to this checkout's path, so the rendered `omui.build.contracts` stayed a registry version that is not published. All six templates (app-cpp, app-ts, app-lua, component, native, theme) build and pass locally |
| 21 | hanim-features and hanim-core-consumer probes: stale after M1/M2 | the consumer steps (nightly lane) | M1 (hanim's move, the namespace pass), M2 (hanim.core reaching mlibs.utils and mlibs.memory) | R | fixed (this checkpoint, found locally): the packed consumer asked for `modules/base/hanim/core`; the core consumer printed with `omui::to_json` (mlibs.log's now); both expected hanim.core's closure without mlibs.utils/memory, and the packed check took hanim.core's own sources (inside the product's directory) for the product's. Both report ok locally, as do the API probe, the play and omui consumers |

### Design input for M2 (motion convergence)

- A style pack's untargeted motion (`[style.X.motions] update = …`) is a `*` rule since 074e3bd: it reaches every node, so any container can morph its whole drawing (the breakout's field after a camera pull-back). The site's creative demos are tuned to this reading; the original one (9d7f442) only restyled existing motions. M0 keeps the site's reading, excludes the window root, and lets applications declare live surfaces; the slot model moved into hanim must state which nodes a style may move.

- A film runs at motion level off so that no morph is inferred from the frame drawn before; since 77979b5 that also drops the cross-fades its timeline asks for (the promo's style switches are cuts now). The film's transitions belong on its own timeline (hanim), not on the framework's inferred motion.

### Upstream issues (filed; reproduced on the latest mcpp, 2026.11.5.1)

- GCC 16.1 (not filed): a function-local using-declaration of an enum from another module (`using mlibs::error::Kind;` inside a `constexpr` function of an interface) is an internal compiler error in `import_entity_module` (cp/module.cc:4361); an alias declaration works. Minimal reproduction kept in the D-109 note.

- [mcpp#896](https://github.com/mcpp-community/mcpp/issues/896): **two packages with the same short name drop one from links.** A package named `xrender` (no namespace) and `compat.xrender` (SDL3's X11 dependency) in one plan: binaries that reach both link without `xrender`'s objects (undefined references), while `-p` of either side alone links. Worked around by naming the package `xrender.graph`.

- [mcpp#897](https://github.com/mcpp-community/mcpp/issues/897): **`[feature-deps]` not applied when the package is also a selected root.** Workspace `a` (features `f1`, `f2` with sources), `v` (`a` with `f1`; feature `x` whose `[feature-deps.x]` asks `a` for `f2`), `s` (`v` with `x`). `mcpp build -p s` builds; `mcpp build -p s -p v` compiles `v`'s `x` sources but not `a`'s `f2` ("a.f2: failed to read compiled module").

## Capability matrix (short)

| Project | One-sentence definition | Evidence state |
|---|---|---|
| omui | One semantic tree drives pixels, strong motion, accessibility, agents and tests | UI eval 63 (2026-11-01); motion not yet on hanim |
| hanim | Composable, agent-native animation engine for UI, video and game animation | eval 62 (before M2); M2: typed channels, springs in programs, time maps, interactive mode with takeover, the visual representation, v1 shape |
| xrender | CPU/GPU same-source, Web-first renderer with agent-reviewable pixels | eval 65; GPU only on lavapipe |
| xengine | Game engine system where people and agents develop and play as equals | eval 62 (game engine) |
| gongfu | Deterministic, rollback-ready, data-driven fighting framework | 10,000-rollback determinism test |

## M2 — global contracts and the hanim kernel (exit review)

| Item | State |
|---|---|
| `mlibs.cts` framework (§5.5) | done (D-108) |
| RHI conformance on mlibs.cts | done: 19 cases, the same results; backends keep the JSON report |
| simulation GPU conformance on mlibs.cts | done: 17 cases (16 scenarios and the refusal) needing `compute`; the same totals and process hash as before (`0769cee00d2cbf3a`); backends require compute |
| spec level S on mlibs.cts | done: the harness is `modules/projects/omui/cts` (was `tests/cts`); `conform()` gives one case a scenario; JavaScript, Lua and embedded Python 7/7 each |
| error contract `mlibs.error` | done (D-109): `Error<Code, Where>{code, message, where, causes}`, kinds = openas error codes; `mlibs.rhi` and the cellular simulation on it |
| handle contract | done: `mlibs::memory::OwnedHandle<Tag>{owner, slot, generation}` beside `Handle<Tag>`; hanim's `ObjectRef` and `PlaybackRef` are it, tagged |
| time contract `mlibs.time` | done (D-110): `seconds`, `Flicks`, `Rate`, exact conversions; `xengine.timeline`'s copies and `hanim::FrameRate` removed, their users on it; lint `time` |
| hanim's Failure family on mlibs.error | done (D-111): five hanim failures and mlibs.geom's `PathFailure` are `Error<Code, Where>`; nested causes are `causes`; wrapping codes take their cause's kind; `*_error_name` removed |
| prepare/execute and budget conventions, contract CTS | done (D-112): core design §10; `mlibs.cts:prepare` cases on hanim (program preparation 7 cases, engine publication 3) and the cellular simulation (10); the suite found two wrong refusal kinds in the cellular simulation, fixed |
| lint `owner` | done (D-113): `tools/lint/capabilities.owners.omui.toml`, six capabilities (fnv1a, splitmix64/fdlibm, flicks, OKLab, the handle's shape, a component animating itself); three private handles and example 05's OKLab moved onto their owners; the `motion` rule had been dead since M1 (it looked for `components/` at the root) |
| hanim L2: typed channels | done (D-114): `hanim::ch`, `anim::to/by(target, ch::x = v, …)`, `part(object, id)`, application channels as part values in `FrameView::values` |
| hanim L1 in programs: springs | done (D-115): `EaseKind::spring`, `.spring(params)`, `anim::spring`; overshooting curves overshoot properties |
| hanim L4: time mapping, first step | done (D-116): `timed`, `loop` (forever too), markers; whole programs only |
| hanim L5: interactive mode, first step | done (D-117): `engine.drive(key, channel)` → `Drive<T>`, the engine's one Set; as fast as Set (bench `motion/drive-1000`) |
| render (xrender) suite on mlibs.cts | done (D-118): `xrender.cts` is the owner's suite library, 47 cases; `xrender.rhi.sdl` runs it (lavapipe 47/47 locally) |
| hanim v1 API: compatibility names removed | done (D-119) |
| hanim `visual` representation | done (D-120): `omui.motion` is `hanim.visual` (its own package in the hanim project: its tests need it; mcpp has no per-package test features) |
| hanim L5: timeline takeover of driven channels | done (D-122): `drive.follow(playback)`; handed back with momentum (`Set::carry`) |
| hanim L4: time maps inside programs | done (D-123): whole courses (integral repeat, alternate, reverse) unrolled into replicas; rate/trim/endless stay root-only |
| hanim v1: one value type | done (D-124): `Program`; time on the expression (`loop`, `alternate`, `reverse`, `rate`) |
| exit: independent consumers | all six probes ok locally (hanim-api-probe, hanim-play-consumer, omui-hanim-consumer, hanim-features 8 cases, hanim-core-consumer, the templates) |
| exit: Set and Morph benchmarks | release, locally: `motion/drive-1000` p95 0.011 ms beside `motion/set-1000` 0.021 ms; `motion/switches-100` 1.59 ms (budget 5.5), `-liquid` 13.4 ms (45), `-ink` 33.9 ms (90) |
| exit: pixel baselines | unchanged: the site (22), the promo, the style gallery, widget-breakout and the markdown wall pass their pins after every M2 checkpoint |
| exit: CI | pending: one all-green run on an M2 commit (cb26e381: every row green but the templates step, fixed in 7283d34a) |

## M3 — UI animation on the one Engine (started)

| Item | State |
|---|---|
| interactive mode has what a host needs (clocked verbs, glides, targets) | done (D-125) |
| the view runtime's values on the host's Engine (no private `Set`) | done (D-126): pins unchanged; `services.engine()` |
| Playback as a light handle on the same Engine | done (D-127) |
| motion slots and recipe ids in the specification | done (D-128; openlifa/openmui 18cd62d) |
| the 37 component kinds, swept | done: `omui.viewdoc`'s component motion sweep builds every kind generically and changes `disabled`: 15 kinds move (a middle neither endpoint at full and reduced, rest exactly motion off's drawing, nothing left moving); `text_box` is immediate as its recipe says. Findings: 21 kinds draw no disabled state at all (tabs, pagination, combobox, tag_input, tree_view, data_grid … — a UI gap, not motion); `progress`'s indeterminate sweep ignored reduced/off motion — fixed: it holds still (`Services::motion_reduced`) |
| style packs naming recipe ids end to end; interruption and hit/focus/IME consistency across kinds; old paths deleted | next |

## M3.5 — hanim representations and deformation (planned, D-129)

| Item | State |
|---|---|
| 1 glyph/path adapter (glyph outlines → hanim parts) | next |
| 2 stroke representation (trim start/end/offset, writing; xrender trims by arc length) | next |
| 3 topology-aligning morph (A→B) · 4 selectors + deformers (overshoot, drag) · 5 match modifiers + path noise | planned |
| 6 blending layers · 7 constraints · 8 GPU evaluation | planned |

## Next

M0 and M2 exit on one all-green CI run of an M2 commit (the matrix rows; the GCC rows and their gated steps in the nightly lane). Then M3 (plan §6.4): the UI runtime's motion stage on the one Engine — node slots declare motion intent, style packs give recipe ids and parameters, the UI gives representations and identity, hanim evaluates; `Set` users move to `drive`; Playback becomes a light handle; the 37 component kinds; old paths deleted; intermediate states, interruption, reduced/off, hit/focus/IME consistency, frame time and Wasm compared. mcpp is pinned to the latest release, 2026.11.5.1 (`.xlings.json`).
