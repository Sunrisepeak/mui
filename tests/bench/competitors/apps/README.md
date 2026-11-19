# omui.bench.competitors.apps

Whole applications side by side (implementation plan C1, lead design §2a task N2): the same app written in omui, Slint, Flutter and NSH, built at pinned versions on one machine, launched in a real window and measured from outside its process. `omui-devtools bench apps` ([tools/devtools](../../../../tools/devtools)) does all of it; the sides and the pins are [`apps.bench.xdef.json`](apps.bench.xdef.json).

```sh
omui-devtools govern -- omui-devtools bench apps --build-only     # every side built and sized (their SDKs into target/bench-apps/sdk, or --sdk DIR)
omui-devtools bench apps --no-build --rounds 10 --xvfb PATH       # launched round after round, judged into the lead ledger
```

It needs, besides each side's own toolchain (Rust through rustup, the system's clang, CMake, Ninja and GTK 3 and 4 with their development files, WebKitGTK 6 and GStreamer's for NSH): an `Xvfb`, `dbus-daemon`, `pmap` and GNU `dd`; `pidstat` (sysstat) for the switches. Here Xvfb 21.1.12 and sysstat 12.6.1 came from Ubuntu's packages unpacked into a private directory, and GTK 4's, WebKitGTK's and GStreamer's `.pc` files from their `-dev` packages the same way (`PKG_CONFIG_PATH`), no system change.

| | |
|---|---|
| `src/` | omui's side, package `omui.bench.competitors.apps`: `hello` (a title and a button), `counter` (the counter of tests/bench's `core/*` scenes), `market` (the live market board of `core/live-dashboard`), `list` (the 10,000 rows of `core/list-scroll`); the world is the package's feature (`full`, `app`, `lean`, `minimal`) |
| `slint/` | Slint 1.19.0's `hello`, `counter` and `list` (`Cargo.lock` pins the rest); its defaults: winit, FemtoVG over OpenGL, the fluent style |
| `flutter/` | Flutter 3.50.0's `hello`, `counter` and `list` (Material 3; `pubspec.lock`); the Linux runner is the one `flutter create` writes for that SDK, its window set to the side's size |
| `nsh/` | NSH's side where NSH has no app of its own for the work: `list`, the same 10,000 rows in its `List`. Its hello worlds are `nsh init`'s templates (`rust`, `ts`), its counter `examples/counter`, its board the gallery's Stocks (`NSH_GALLERY_DEMO=Stocks`), all at commit `83d5c4a5` (0.6.0) |

## How it is measured

- **The display.** An X server the harness starts (Xvfb 21.1.12, 1920 × 1080 × 24, no window manager, no compositor) and a session bus, as a desktop has. The harness is a client of the same server: DAMAGE tells it whenever anything is drawn on the screen, GetImage reads what is there, XTEST presses the buttons. A frame counts as presented when the server has carried out the drawing.
- **Graphics.** GL reaches Mesa (25.0.7's llvmpipe with LLVM 20.1.7, software; the session's `LIBGL_DRIVERS_PATH`): NVIDIA's EGL makes no context on a server without DRI3, so every GL side (Slint's FemtoVG, Flutter's Impeller, NSH's GTK 4) draws on the CPU here. Vulkan reaches the RTX 4080 (omui's `--gpu` sides), presenting through the same server. Each run records what its process mapped.
- **Start**: from launching the program to the first presented frame equal to the settled picture (nothing drawn for 0.6 s after it). Cold: the package's files and every file the process mapped on its previous run dropped from the page cache first (`dd iflag=nocache`, where no other process maps them); warm: launched again at once.
- **Memory at rest**, 5 s after that frame: `pmap -XX`'s totals of the process and its children — private dirty (what it wrote and alone holds), USS (private clean and dirty), PSS, RSS.
- **At rest**, the next 5 s: processor time (mcpplibs.os), the threads' context switches a second (`pidstat -w -t`), frames drawn on the window.
- **Click**: the pointer on the button, pressed, everything settled, let go; from letting go to the first presented frame whose count (`label`) changed. Five a run.
- **In motion** (`market`, `list`): the same 5 s while it moves — the board by itself, the list with the wheel turned every 1/120 s — the frames presented a second, the intervals between them, the processor.
- **Bytes**: every program and library of the package, each stripped (`strip --strip-all`), and the package directory as `tar.gz` (gzip's default level), the same archiver for every side.
- Ten rounds alternate the sides in one job; each metric is judged by the ratio a round (lead design §2.2) into [the ledger](../../../../.agents/docs/evaluation/lead.ledger.xdef.jsonl).

Builds: omui with mcpp's toolchain (`dist`: GCC 16, `-O3`, sections collected); Slint and NSH's Rust with the system's Rust toolchain, link-time optimisation (fat), one code unit, stripped — as NSH's report builds its hello worlds; Flutter `flutter build linux --release`.

## Results

2026-11-19, 10 rounds (12:00–12:45), medians of the rounds (the 90th percentile of the cold start in brackets); bytes as shipped, each program stripped. The main session's builds were held for the run; another session's compiles came and went (1-minute load 0.5–29, logged every 20 s). The ledger's lines are this run's (`.agents/docs/evaluation/lead.ledger.xdef.jsonl`, `ours` = `omui-app`, the default world).

| side | app | programs MB | package MB | start cold ms (p90) | warm ms | private dirty MiB | USS | PSS | RSS | CPU at rest % | switches/s | click → frame ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| omui-full | hello | 18.50 | 8.77 | 334 (445) | 82 | 62.1 | 161.8 | 163.8 | 167.2 | 0.03 | 10.4 | — |
| omui-app | hello | 17.70 | 8.42 | 324 (426) | 84 | 62.1 | 161.5 | 163.7 | 166.8 | 0.03 | 10.4 | — |
| omui-lean | hello | 13.45 | 6.56 | 199 (213) | 59 | 50.0 | 112.7 | 114.5 | 116.3 | 0.00 | 0.4 | — |
| omui-minimal | hello | 12.96 | 6.35 | 46 (49) | 17 | 14.7 | 24.0 | 25.7 | 27.5 | 0.00 | 0.4 | — |
| omui-app | counter | 17.69 | 8.42 | 319 (336) | 82 | 62.1 | 161.2 | 163.4 | 166.6 | 0.03 | 10.4 | 2.0 |
| omui-minimal | counter | 12.95 | 6.34 | 44 (48) | 16 | 14.7 | 23.9 | 25.4 | 27.5 | 0.00 | 0.4 | 1.5 |
| slint | hello | 17.47 | 8.62 | 160 (208) | 70 | 49.7 | 82.6 | 97.8 | 133.6 | 0.00 | 2.0 | — |
| slint | counter | 17.47 | 8.62 | 158 (220) | 69 | 49.6 | 82.5 | 97.7 | 133.5 | 0.00 | 2.0 | 1.4 |
| flutter | hello | 20.76 | 8.80 | 250 (366) | 139 | 137.0 | 179.3 | 195.7 | 245.2 | 0.00 | 0.0 | — |
| flutter | counter | 20.76 | 8.81 | 263 (380) | 200 | 137.0 | 179.4 | 195.8 | 245.1 | 0.00 | 0.0 | 16.5 |
| nsh-rust | hello | 22.43 | 10.08 | 190 (241) | 107 | 54.7 | 87.7 | 106.3 | 155.8 | 0.00 | 0.0 | 26.3 |
| nsh-ts | hello | 30.22 | 13.25 | 265 (326) | 112 | 60.1 | 107.1 | 126.1 | 178.8 | 0.00 | 0.0 | 24.0 |
| nsh-ts | counter | 30.22 | 13.25 | 236 (310) | 107 | 60.7 | 107.6 | 126.6 | 179.3 | 0.00 | 0.0 | 21.1 |

`hello` and `counter` are NSH's own templates (both count; their button is the click); Slint, Flutter and omui's `hello` has no click. In motion, the same 5 s:

| side | app | frames/s | present p50 ms | p95 | p99 | CPU % | CPU ms a frame | private dirty MiB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| omui-app | market | 60.2 | 16.7 | 18.2 | 18.7 | 118 | 19.6 | 85.0 |
| omui-app | list | 40.9 | 19.7 | 49.7 | 62.6 | 246 | 60.8 | 125.2 |
| slint | list | 62.4 | 16.0 | 17.3 | 17.9 | 126 | 20.2 | 67.3 |
| flutter | list | 41.7 | 24.3 | 33.8 | 35.1 | 698 | 166.1 | 210.1 |
| nsh-ts | market | 49.3 | 19.7 | 29.4 | 35.1 | 441 | 86.8 | 141.3 |
| nsh-ts | list | 60.1 | 16.7 | 18.5 | 21.5 | 110 | 18.0 | 79.0 |

The board is not the same work on both sides: omui's is tests/bench's (22 sparklines, a 40-level book, a 30-row tape, a tick every 1/120 s), NSH's is its gallery's Stocks (watchlist, intraday chart, book, tape, springs; a tick every 50 ms) — the same class of scene (§2a's metric 1), compared as such.

**The ledger's verdicts** (the ratio a round, theirs ÷ ours, against the strongest; tiers of lead design §2.2):

| Metric | omui (`app`) | strongest | ratio [95 %] | verdict |
|---|---:|---|---|---|
| programs, stripped | 17.70 MB | Slint 17.47 | 0.99 [0.99, 0.99] | par |
| package, tar.gz | 8.42 MB | Slint 8.62 | 1.02 [1.02, 1.02] | par |
| start, cold | 324 ms | Slint 160 | 0.49 [0.43, 0.61] | behind |
| start, warm | 84 ms | Slint 70 | 0.86 [0.79, 1.22] | behind |
| private dirty at rest | 62.1 MiB | Slint 49.7 | 0.80 [0.80, 0.80] | behind |
| USS, PSS at rest (reported) | 161.5, 163.7 MiB | Slint 82.6, 97.8 | 0.51, 0.60 | behind |
| processor at rest | 0.03 % | Flutter, NSH 0 | 0 | behind (NSH's floor 0–5 %: within) |
| switches a second at rest (reported) | 10.4 | Flutter, NSH 0 | 0 | behind |
| click → frame | 2.0 ms | Slint 1.4 | 0.68 [0.65, 1.18] | behind (Flutter 16.5, NSH 21–26: 8–13× ahead of them) |
| live board, present p95 | 18.2 ms | NSH 29.4 | 1.62 [1.29, 2.64] | ahead (not the same board) |
| live board, CPU a frame | 19.6 ms | NSH 86.8 | 4.15 [3.54, 4.92] | ahead (not the same board) |
| 10k list scrolled, present p95 | 49.7 ms | Slint 17.3 | 0.35 [0.30, 0.42] | behind |
| 10k list scrolled, CPU a frame | 60.8 ms | NSH 18.0 | 0.30 [0.22, 0.34] | behind |

`lean` and `minimal` are ahead of every side on bytes (13.45 and 12.96 MB, 6.56 and 6.35 MB packed), and `minimal` — no GL — on start (46 ms cold, 17 warm) and memory (14.7 MiB private dirty); the ledger judges `app`, the default.

**Under load** (11:07–11:56, the main session's builds running; 1-minute load 12.7 at the start, 6.0 at the end): the same sides, the same 10 rounds, kept apart from the ledger.

| side | app | start cold ms | warm ms | click ms | frames/s | present p95 ms |
|---|---|---:|---:|---:|---:|---:|
| omui-full | hello | 359 | 93 | — | — | — |
| omui-app | hello | 363 | 94 | — | — | — |
| omui-lean | hello | 212 | 61 | — | — | — |
| omui-minimal | hello | 45 | 18 | — | — | — |
| omui-app | counter | 341 | 126 | 2.7 | — | — |
| omui-minimal | counter | 42 | 23 | 2.3 | — | — |
| omui-app | market | 418 | 107 | — | 60.2 | 18.3 |
| omui-app | list | 369 | 127 | — | 42.8 | 43.2 |
| slint | hello | 165 | 70 | — | — | — |
| slint | counter | 164 | 67 | 1.6 | — | — |
| slint | list | 187 | 96 | — | 62.4 | 17.4 |
| flutter | hello | 278 | 140 | — | — | — |
| flutter | counter | 265 | 140 | 22.0 | — | — |
| flutter | list | 300 | 185 | — | 44.7 | 31.3 |
| nsh-rust | hello | 215 | 81 | 25.2 | — | — |
| nsh-ts | hello | 223 | 103 | 24.1 | — | — |
| nsh-ts | counter | 233 | 109 | 21.7 | — | — |
| nsh-ts | market | 238 | 124 | — | 51.4 | 30.5 |
| nsh-ts | list | 289 | 111 | — | 60.0 | 18.3 |

## What the runs found in omui

- **The package does not start on this machine's system.** mcpp's toolchain links its own C library (glibc 2.44), so the packed program asks for `GLIBC_2.43`; Ubuntu 24.04 has 2.39 (`version 'GLIBC_2.43' not found`). A self-contained package (`--mode self-contained`, the C library carried) is 52 MB; a build against the system's (`--sysroot glibc@system`) fails in SDL3 (`posix_spawn_file_actions_addchdir` undeclared). What runs here is the same program as mcpp built it, beside the package's fonts — owed: a package that starts on the oldest supported system.
- **`app` and `full` load every Vulkan driver at start, though the window draws on the CPU.** The GPU path's provider opens the Vulkan loader, which loads each installed driver (here NVIDIA's, Mesa's RADV and lavapipe with LLVM 22 and ICU): 89 libraries at start against `lean`'s 46 (`LD_DEBUG=files`). `app` starts in 324 ms cold and 84 warm against `lean`'s 199 and 59; how much of that the drivers take is not measured apart.
- **`app` and `full` wake ten times a second at rest** (`lean` and `minimal` 0.4): Slint 2, Flutter and NSH 0. The processor is still about 0.03 % of a core.
- **Without GL a window is far cheaper.** `minimal` presents its CPU frames with SDL's software path (shared memory, no GL): 14.7 MiB private dirty and a cold start near 40 ms. Every GL path here is Mesa's llvmpipe, whose context is most of the other sides' private dirty memory.
- **`--gpu` does not start on this display.** With the RTX 4080 alone SDL_GPU finds no backend for an X server without DRI3 (Vulkan's own vkcube presents there); with lavapipe listed too it faults inside LLVM. The GPU rows are unverified here (D-201).

## Not measured here, and why

- A real GPU's GL and a real display: NVIDIA's EGL needs DRI3, Xvfb has none, and this machine runs no display server. Every GL side draws with llvmpipe, so the frame rows say what each does on a CPU, not on the RTX 4080; a real display's numbers are the CI rows' or a desktop's (`omui-devtools bench apps` runs unchanged there).
- 120 Hz: Xvfb presents GL at 60 Hz, so a side that waits for the display shows about 16.7 ms between frames whatever it could do.
- Input from a device: the clicks are XTEST's, from the server; the time from a physical press to the server is the same for every side and not in the number.
- NSH's own timers (engine start, its first frame, event to commit) are inside its process; the start and the click here are the consensus' (§2a.1), from outside. Hot reload (C8) and the Web (C3) are not in this run.
- mtest's monitoring measures a case's own process (its processor time and largest resident set); another process's PSS, USS and private dirty, its context switches and its window's frames are not in it, so the harness reads them itself (`pmap`, `pidstat`, the X server). The first two are a general gap for mcpplibs.os (a program's memory accounting and switches) and mtest upstream.
