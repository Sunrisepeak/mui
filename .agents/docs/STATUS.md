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

## M1 — structure and naming (started beside M0's last red items)

| Step | State |
|---|---|
| ① lint path→layer table; `mlibs.utils`; fuzzy into its consumer | this checkpoint (D-101): layers by name from a path-prefix table holding today's and M1's directories; hash, log, trace, testing → `modules/base/utils`, every tool built (a feature per tool recompiled the graph per member selection, measured); lint `testing` (test checks only in `tests/`) and `closure` (render and simulation closures, from the retired `tools/ci/foundation-migration`); `mlibs.fuzzy` → `openmui.native.fuzzy:matching` |
| ② hanim and xrender into `modules/projects/` | next |
| ③ xengine | |
| ④ omui with components, lang, sdk, templates | |
| ⑤ namespace rename, forwarders removed, lint `legacy-import` | |

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
| 9 | GCC dev/release jobs cancelled at 2 h | Linux GCC | — | F | addressed: rows no longer run the site (its 4 tests timed out at 300 s each), limit 150 min; to confirm |
| 10 | Pages: `--locked` resolution lacks accesskit | Pages | `12d2e15` | E | build and delivery budget fixed (4,886,971 B). Browser checks: CI's Chrome refuses clipboard reads, so the scripts observe the page's own clipboard writes (a refused read can no longer pass as `{}`), and NoScript navigation is awaited. Local Chromium: presentation 159/159, motion 151/151. Confirmed: Pages deployed from 75655b7 |
| 11 | cellular reaction kernel wrong on Direct3D 12 / DXBC (WARP): tick 1, index 515 (channel 2 of the one active cell) GPU 0, CPU 1 | Windows `rhi.sdl` | never validated (Windows did not build since 11-04) | R | open: CI's Windows row now runs lavapipe as intended (`SDL_GPU_DRIVER=vulkan`); the D3D12 path needs its own run. The rhi CTS passed on D3D12 |
| 12 | agent e2e: `delete-all` by an agent ran without asking | Linux (example 07) | 20e6f54 (development mode is full access) | T | fixed: the acceptance starts the example in dual-user mode, the needs-answer path it checks; the whole e2e passes locally |
| 13 | every GPU test on Windows: "SDL_HINT_GPU_DRIVER vulkan unsupported!" (lavapipe, 26 checks) | Windows | 75655b7 (first lavapipe run) | E | fix to confirm: the payload's manifest names `.\\vulkan_lvp.dll` and relies on the installer's rewrite; a manifest the loader cannot parse or resolve is skipped silently. `tools/ci/gpu.sh` writes its own (absolute, forward slashes), makes the device under SDL's windows video driver and turns the loader's diagnostics on |
| 14 | Python suite hangs 240 s in `test_the_scenarios_record_their_traces` | macOS (intermittent) | ≤ 75655b7 | R | fixed: a handler on the script thread that called the runtime kept the GIL while it waited for the runtime's thread, which was waiting for the GIL to return from `act`. The binding now always releases the GIL around the session's work; a new test (200 acts whose handler invokes a verb) hangs on the old code on Linux and passes 3/3 with the fix |

### Design input for M2 (motion convergence)

- A style pack's untargeted motion (`[style.X.motions] update = …`) is a `*` rule since 074e3bd: it reaches every node, so any container can morph its whole drawing (the breakout's field after a camera pull-back). The site's creative demos are tuned to this reading; the original one (9d7f442) only restyled existing motions. M0 keeps the site's reading, excludes the window root, and lets applications declare live surfaces; the slot model moved into hanim must state which nodes a style may move.

- A film runs at motion level off so that no morph is inferred from the frame drawn before; since 77979b5 that also drops the cross-fades its timeline asks for (the promo's style switches are cuts now). The film's transitions belong on its own timeline (hanim), not on the framework's inferred motion.

### Upstream issues to report (awaiting the maintainer's go-ahead)

- **mcpp 2026.11.2.2: `[feature-deps]` not applied when the package is also a selected root.** Workspace `a` (features `f1`, `f2` with sources), `v` (`a` with `f1`; feature `x` whose `[feature-deps.x]` asks `a` for `f2`), `s` (`v` with `x`). `mcpp build -p s` builds; `mcpp build -p s -p v` compiles `v`'s `x` sources but not `a`'s `f2` ("a.f2: failed to read compiled module").

## Capability matrix (short)

| Project | One-sentence definition | Evidence state |
|---|---|---|
| omui | One semantic tree drives pixels, strong motion, accessibility, agents and tests | UI eval 63 (2026-11-01); motion not yet on hanim |
| hanim | Composable, agent-native animation engine for UI, video and game animation | eval 62; strict 2D object model, 4 channels |
| xrender | CPU/GPU same-source, Web-first renderer with agent-reviewable pixels | eval 65; GPU only on lavapipe |
| xengine | Game engine system where people and agents develop and play as equals | eval 62 (game engine) |
| gongfu | Deterministic, rollback-ready, data-driven fighting framework | 10,000-rollback determinism test |

## Next

M1 ② (hanim and xrender into `modules/projects/`, raster onto `mlibs.math`, text without the renderer); M0's remaining red items as CI reports them. CI's PR lane is far from §10.1's 30 minutes (tests ~70 min per x64 row): after M1, move GCC release and the consumer harnesses to Nightly.
