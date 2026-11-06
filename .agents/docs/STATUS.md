# STATUS

The single progress entry (plan §12, at most 200 lines). Updated by every checkpoint commit on PR #1 (`openmui-design`). Decisions and the full roadmap: [top-level convergence plan](2026-11-06-top-level-assessment-convergence-design.md).

## Current milestone: M0 — stop the bleeding

Goal: the whole CI matrix green once, Pages deployed. CI does not block progress, but every round starts by reconciling the previous checkpoint's CI (plan §10.1).

| Item | State |
|---|---|
| Convergence plan, AGENTS.md rewrite, STATUS | done (this checkpoint) |
| `mcpp.lock` untracked and ignored; `site.sh` without `--locked` | done |
| CI red list below | in progress |

## CI red list

Baseline: `810d1fc` (last all-green CI: `26d1e9a`, 2026-11-01). Kind: R = regression, E = environment, F = flaky/time.

| # | Failure | Where | Since | Kind | State |
|---|---|---|---|---|---|
| 1 | `WeightedPaint` incomplete type in `std::span` arithmetic | Windows build, omui pack | ≤ `c9b0d1c` | R | fixed, to confirm on Windows CI: MSVC STL checks C++23 const-iterator concepts when `std::span<const T>` is instantiated, so a span of the then-incomplete `WeightedPaint` cannot be a `Paint` member; `mlibs::memory::View<T>` replaces it in raster and render |
| 2 | `hanim.render.vector.pcm` has no rule (view's `hanim` feature) | Web build (`host/web`, `site`) | ≤ `810d1fc` | R | open |
| 3 | Metal: cellular `rules.slang` buffer index 0 reserved twice | macOS `rhi.sdl` | ≤ `ab4d823` | R | open |
| 4 | `viewdoc/slots are filled by code` (`viewdoc_test.cpp:378`) | all | ≤ `074e3bd` | R | fixed: the test predates default exit motions; replaced content is `removing` (S-4) until it settles |
| 5 | widget-breakout: room 3 hash, run flow, script reload (6 checks) | all | ≤ `074e3bd` | R | open |
| 6 | promo-video pinned frames (`promo_test.cpp:129/133`) | all | ≤ `c9b0d1c` | R | open |
| 7 | style-gallery liquid tab lens stretch (`styles_test.cpp:143`) | all | `77979b5` (bisected) | R | fixed: `Matter::follow_layout` replaced every style-wrapper shape with the target's; only allocation carriers adopt now, state shapes (the lens) keep morphing |
| 8 | site motion/site tests exceed 300 s | Linux x64/arm | — | F | open |
| 9 | GCC dev/release jobs cancelled at 2 h | Linux GCC | — | F | open |
| 10 | Pages: `--locked` resolution lacks accesskit | Pages | `12d2e15` | E | fixed by lock policy, to confirm |

## Capability matrix (short)

| Project | One-sentence definition | Evidence state |
|---|---|---|
| omui | One semantic tree drives pixels, strong motion, accessibility, agents and tests | UI eval 63 (2026-11-01); motion not yet on hanim |
| hanim | Composable, agent-native animation engine for UI, video and game animation | eval 62; strict 2D object model, 4 channels |
| xrender | CPU/GPU same-source, Web-first renderer with agent-reviewable pixels | eval 65; GPU only on lavapipe |
| xengine | Game engine system where people and agents develop and play as equals | eval 62 (game engine) |
| gongfu | Deterministic, rollback-ready, data-driven fighting framework | 10,000-rollback determinism test |

## Next

M1 structure and naming (five checkpoints, plan §14): lint path→layer table and `mlibs.utils`; hanim and xrender; xengine; omui with components, lang, sdk, templates; namespace rename and forwarder removal.
