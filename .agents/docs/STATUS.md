# STATUS

The single progress entry, at most 200 lines (lint `status`). Each checkpoint updates one task line (≤ 600 characters, with an evidence pointer: a commit, a case or a benchmark); its details are in the commit body. The plan, its rules and its tasks: [convergence and first release](2026-11-15-convergence-first-release-design.md) (D-189–D-202). Everything before it — the M0–M6 milestones, their tasks, reviews and closed rows — is in the [archive](STATUS-archive.md).

## Stage: RC0 — reconcile and make current (since 2026-11-15)

Exit (node N0): one CI run green on every tier-1 row (unverified cells by the plan's §2.5); STATUS ≤ 200 lines.

| Task | State | Evidence and result |
|---|---|---|
| RC0.1 push the five local commits; watch the run | in progress | pushed with the plan at `d71dc68d`; run 43704244752: Web red again on red 96 (the same crowd hash as before the wrap's fix: the divergence is not Detour's trigonometry) |
| RC0.2 R12, the CI reading tool | done | `tools/devtools` (`omui-devtools ci run ID`): run 43685505783 read as by hand — nav (red 96), `present_test` and WKWebView (55), `tests/bench` (82), and the three new ones (rows 98–100); `devtools.ci` 3 cases |
| RC0.3 macOS `quay.desktop`: root cause, fix or label | done | red 98 is the test's: it fixed `offscreen`, where SDL's Metal backend needs the driver's `Metal_CreateView` (SDL 3.4.18 `METAL_PrepareDriver`); it now takes `OMUI_GPU_VIDEO_DRIVER` as every device test does (`cocoa` on macOS CI, `tools/ci/gpu.sh`); CI to confirm |
| RC0.4 heavy cases within the limit (`emergence3d` case 6, red 97) | done | `emergence3d_test` 113 → 22.7 s (dev, x64): dynamics, particles, field, matter at `-O2` as cellular and the mesh tracer are, the same bits (dynamics and matter CTS pinned values pass); red 97: the tracer shares rows among threads (`Trace::workers`, bit-identical, a new case says so), `mesh_test` 48 cases in 27.8 s; CI to confirm |
| RC0.5 red 55 by root cause or label; red 96 confirmed on CI | done (CI to confirm) | red 55, two causes: the host's software renderer turned SDL's framebuffer acceleration off under every driver, and cocoa, Wayland, KMS/DRM and Android have no framebuffer but the accelerated one (fixed in `omui.host.sdl3`); `present_test`'s font case wrote 187 KB on the thread that reads it, past macOS's 8 KB socket buffer (`sent_and_received`); CI to confirm |
| RC0.6 the other tier-1 reds (Windows 48, 65, 82; Pages 33, 56; 54) | in progress | 33/56: openkal-emscripten 0.14.3 (#22, tables to `.bss`; mcpp-index#799 merged), Pages to confirm; 65, 82 upstream (mcpp#926, #936 open); 48, 54 passed on run 43685505783; red 100 closed: `project_test` writes a broken manifest on purpose |
| RC0.7 a local LLVM compile-only check | done (used before this push: 5 members, 54 s) | `omui-devtools llvm`: the change's members (files differing from `@{upstream}`, and new ones) and their direct consumers, tests included, compiled with CI's LLVM and `ci` profile, not run; `devtools.members` 4 cases |
| RC0.8 STATUS slimmed, capability matrix by slices, lint `status` | done | this file; the old one moved whole into the archive; lint `status` |
| RC0.9 the confirmed decisions into AGENTS | done | AGENTS §7 (D-189–D-202) |
| RC1.1 labels in every manifest, project READMEs' label tables, lint `maturity` (ahead: metadata only) | done | 175 manifests: 102 stable, 58 preview, 15 experimental (§2.3); 6 stable packages name the preview parts they use (`lower`); experimental READMEs say target, gap, graduation; self-test 40 of 40 |
| RC2.12 Glade's basic gate: T5.8b (ahead: no prerequisite) | done | `showcase/glade/headless`: the valley at `xengine.runtime`, a hand (pointer aims; torch, pour, push, rain, lightning, rod; wind); an agent reading only the view meets across, campfire (after rain) and ice in 3 branches, each `why` back to the hand; `glade.headless` 3 cases, 33.8 s; storm not met (R11) |
| RC2.6 a magnitude in every 2D emergence case (ahead) | done | `matter.emergence` 13 cases: each holds one number to a hand calculation printed beside it, tolerance written — exact: ash 400 ticks after catching (fuel over rate), 28 × 200 cell-ticks alight, 9 cells displaced; estimates: water's mean row 10.35 (10.34), doused in 16 (12), steam ≤ lava's heat (14 of 39.6), frozen in 117 (88), stone/iron 2.43 (1–4), powder 31 (21–30), vapour in 15 (10.7), hottest 1188 K (≤ 1200) |
| RC2.2 KTX2's blocks straight to the device (ahead of N1) | done (browser at the node) | `mesh.blocks` (xrender.mesh.gpu.cts, 21 cases): a KTX2 picture goes as BC7/ASTC/ETC2 where the device samples it (`asset::Options::blocks`, `gpu::blocks_of`, `format_of`); the reference reads the file's own sizes (`Texture::made_of_sizes`); RTX 4080 and lavapipe within the tiles' bounds, as the picture as light; 1024² every size 1.33 MiB vs 10.67, loaded 0.57 vs 27.5 ms; platform row `device-blocks` |
| RC1.2 the platform table and `unverified` (ahead) | done | `docs/platforms.omui.json`: 9 capabilities × 6 platforms, each cell real/software/unverified with its evidence or what is missing; `omui-devtools platforms` writes `docs/{en,zh}/platforms.md` (a test holds them current); lint `platform-table`; the lead ledger's `pending_hardware` is `unverified`; mtest needs nothing new: a skip's reason `unverified: …` says it |
| RC1.3 the package inventory (ahead) | done | 176 members (omui 36, xengine 25, xrender 20, mlibs base 16, hanim 9, gongfu 7, showcases 18, examples 12, tools 5, mcpp 4); by the three rules: 18 variants not proving a closure, 8 CTS packages used by at most their owner, 9 single-consumer small libraries — 34 candidates (176 → about 142); the rest of the way to ≤ 130 is RC1.5's per-project review; the list is in commit `38df17bd` |
| RC1.4 lint `module-closure` (ahead: no prerequisite) | done | a package's interface re-exports only what its manifest lists (`[package.metadata.openmui] reexports`); 46 packages declare theirs; at `f8f9527c` (T1.2) it reports `xpbd.cppm: re-exports xengine.matter`; self-test 39 of 39 |
| RC1.7 design documents' status, two summaries (ahead) | done | `.agents/docs/README.md`: a status column — 8 有效, 8 已被取代 (by whom), 45 历史; one-page summaries of the foundation design (`2026-11-12-foundation-summary.md`) and the TS-first convergence (`2026-11-09-agent-era-ts-first-summary.md`) |

## Node records

None yet: N0 is the first. Each node gets a block of at most 12 lines: date and head; the CI run and each row's verdict; the eight counters; labels changed; new reds and what was done; departures from the plan and why; the next stage re-estimated.

## Counters (plan §3.3)

| Counter | 2026-11-15 (RC0 start) | 0.1 target |
|---|---|---|
| Packages | 176 (with `omui.devtools` and `glade.headless`) | ≤ 130 |
| Open reds, tier-1, stable and preview | 8 rows (54, 55, 56/33, 82, 96, 97, 98, 99) and 48, 65 to confirm | 0 |
| Lint problems | 0 | 0, the five new rules included |
| STATUS lines | under 200 (was 407) | ≤ 200 |
| Debug configuration size | 22 GB measured at RC0 start (9.5 GB in the current one) | ≤ 12 GB |
| Unverified cells, tier-1 | not counted (RC1.2) | only fall; each with its graduation |
| Public claims without evidence | not counted (RC3.4) | 0 |
| Closure checklist items passed | not counted (RC2.14–RC2.18) | all, exceptions written |

## Capability matrix (by slice; labels by the plan's §2.3)

| Slice | Works | Label | Where it stands (2026-11-15) | Missing for 0.1 (plan §5.2) |
|---|---|---|---|---|
| Application | Wall, Styles; 12 examples | stable / preview | runs headless and in windows on four OSes; Web host | disabled state for 21 components; stroke quality against resvg; Web first-frame split; site delivery within budget (33, 56) |
| Agent and Live Design | examples 13, 07; the promo video | preview | endpoints self-describing (D-186), six cold starts measured | `omui.config.ts` openness end to end; W4 re-measured; three cold-start tasks (RC5.3) |
| 2D world | Kiln, widget breakout | preview | headless play, fork and causes; matter, cellular, 2D bodies | Kiln's window host, sound, levels as TS data; magnitude assertions for the emergence set |
| 3D world | Quay | preview | CPU reference and device (lavapipe, RTX 4080, Apple Paravirtual offscreen); rigid3, nav, skeleton, particles, spatial audio | macOS window device (98); Web frame time ≤ 16.7 ms; KTX2 blocks on the device; content and gamepad |
| Experimental | fighting and gongfu, Glade and 3D emergence, Swarm | experimental | gongfu: 10,000-rollback determinism; Glade: four tasks headless (T5.8a); Swarm: one prompt, 538 s, 55 turns, no picture | each: the basic gate (builds on tier-1, one headless check, README experimental section) |

## Carried in-flight work (plan §3.2)

- **R11** (heat by mass and surface): the first attempt is measured and stashed (`stash@{0}`, design §4.2 of M6.5); combustion's heat to the air and radiation come first. Not a release blocker: Glade's known limitation; numbers go into its experimental section if it has no result (RC2.12).
- **T5.8b** (Glade's headless runtime and agent): Glade's basic gate (RC2.12).
- **M6.3's leftovers** (fidelity scenarios outside the bound, KTX2 blocks on the device): KTX2 is RC2.2; the rest after 0.1.
- **D-186's rest**: the script's `task()` (a spec addition); T2.8's prompt on another engine.
- **Zoom (§5.9)**: the kept run for the other primitives; banding and curve areas.

## CI red list (open rows; closed ones and their history are in the archive)

Kind: R regression, E environment, F flaky or time, T test expectation.

| # | Failure | Where; since | Kind | State |
|---|---|---|---|---|
| 2 | `hanim.render.vector.pcm` has no rule (view's `hanim` feature) | Web build; ≤ `810d1fc` | E (mcpp) | worked around: the site declares its direct `hanim` features |
| 9 | GCC rows past 150 minutes | Linux GCC | F | GCC rows nightly and on demand; LLVM rows per push |
| 11 | cellular reaction kernel on Direct3D 12 (WARP): tick 1, index 515, GPU 0 against CPU 1 | Windows `rhi.sdl` | R | open: fix, or Windows declares Vulkan only and D3D12 unsupported (RC2.15) |
| 30 | Pages: the site in a real browser hung after the reaction workspace's phone canvas | Pages; e5cc9ed4 | F | open: every CDP call has a deadline and names what it asked; the next stall says where |
| 33 | Pages: the site's delivery 4,009,332 B over its 4,000,000 B budget | Pages (site-perf) | R | fix in the owner (RC0.6): openkal-emscripten's rights tables had -1 defaults, so ~80 KiB of initialised data in every Web build; zero defaults move them to `.bss` (emsdk 6.0.9) — mcpplibs/openkal-emscripten#22, 0.14.3 |
| 48 | Windows: examples/02-hello-ts `generation_e2e_test`, the application's endpoint | Windows; 53efd9e5 | R | passed on run 43685505783; closed when the next run passes too |
| 50 | Android: `ld.lld` cannot find `-lpthread` | Android | E | worked round: an empty libpthread.a; mcpp-index#750 |
| 54 | lang.ts `strip_test` "four times the text takes about four times as long" | Linux x64, Windows; 5106891d | F | passed on run 43685505783; RC0.6 |
| 55 | macOS: `omui.sdk`'s `present_test` killed at 300 s; `omui.host.sdl3`'s WKWebView case: "SDL_CreateRenderer: Window framebuffer support not available" | macOS; 5106891d, again on 43685505783 | R | fixed (RC0.5): both causes found and fixed; CI to confirm |
| 56 | Nightly ASan + UBSan; Pages "the site in a real browser" fails on delivery alone (red 33) | Nightly, Pages; 5106891d | R | open (with 33) |
| 61 | README media: `promo-video/media/film.webp` is not what the tests draw (813,744 against 813,512 B) | local | T | open: the committed film stays until the change that moved it is known |
| 65 | Windows: no esbuild for `strip_test` though the workspace declares it | Windows; e1749f83 | E (mcpp#926) | mitigated: the row names the installed esbuild (`OMUI_ESBUILD`) |
| 67 | Linux x64 and macOS rows cancelled at 150 minutes | e1749f83 | F | mitigated: the limit is 240 minutes |
| 82 | Windows: a member's package did not build — mcpp rebuilds a dependency module another process has mapped ("user-mapped section"); `tests/bench`, `showcase/quay/picture` | Windows; cc99d708, again on 43685505783 | E (mcpp#936) | upstream; RC0.6 |
| 87 | `mesh.glass` against the reference's exact way: 18.7% of the ball's pixels further than 6 levels | the measure, 2026-11-15 | T | open: the bound is the measurement, to be lowered by the work |
| 96 | `xengine.nav`'s pinned crowd (`0x9299f973ab81ffc6`) other bits on Web and Windows | Web, Windows; b229ad7d | R (the wrap's) | fixed in the owner: Recast/Detour sorted with C `qsort` (unstable; musl and the UCRT order ties unlike glibc/macOS) — recastnavigation-m 1.6.0.2 (stable sorts, a tiled-hall test that caught it on wasm32; mcpp-index#800), pinned here; nav CTS 26 pass native and wasm32 locally; CI to confirm |
| 97 | `xrender.mesh`'s "a ball under a picture of light, drawn and followed along rays" killed at 300 s | Linux x64; b229ad7d | F | fixed (RC0.4): the tracer's rows on every thread, the same bits; `mesh_test` 27.8 s locally; CI to confirm |
| 98 | `quay.desktop` on macOS: "OMUI_REQUIRE_GPU=1 and no device: … No supported SDL_GPU backend found!" while `device_test` draws on the Apple Paravirtual device | macOS; e8660f4c (run 43685505783) | T | fixed (RC0.3): the test fixed the `offscreen` driver; CI to confirm |
| 99 | `xengine.dynamics.cts`' `emergence3d_test` killed at 300 s after case 5 | Linux arm; e8660f4c (run 43685505783) | F | fixed (RC0.4): 113 → 22.7 s locally, the kernels at `-O2`; CI to confirm |

### Upstream issues

- GCC 16.1 (not filed): a function-local using-declaration of an enum from another module inside a `constexpr` function of an interface is an ICE in `import_entity_module`; an alias declaration works.
- mcpp#896: two packages with the same short name drop one from links (worked around: `xrender.graph`). mcpp#926: esbuild not found by tests on Windows (65). mcpp#936: a member's build rebuilds a mapped module (82). mcpp#874: target size and rebuild cascades. mcpp-index#750: `-pthread` on Android (50).

## For 0.2 (found during 0.1; one line each, with the task that found it)

- (none yet)

## Next

1. RC0.1: reconcile run 43704244752 with `omui-devtools ci run 43704244752` when it ends; confirm 96, 48, 54; record the rows' verdicts.
2. RC0.3 (98) and RC0.4 (97, 99), then RC0.5–RC0.7; N0 after them.
