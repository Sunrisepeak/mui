# omui.bench.competitors.apps

Whole applications side by side (implementation plan C1, lead design §2a task N2): the same app written in omui, Slint, Flutter and NSH, built at pinned versions on one machine, launched in a real window and measured from outside its process. `omui-devtools bench apps` ([tools/devtools](../../../../tools/devtools)) does all of it; the sides and the pins are [`apps.bench.xdef.json`](apps.bench.xdef.json).

```sh
omui-devtools govern -- omui-devtools bench apps --build-only     # every side built and sized (their SDKs into target/bench-apps/sdk, or --sdk DIR)
omui-devtools bench apps --no-build --rounds 10 --xvfb PATH       # launched round after round, judged into the lead ledger
```

It needs, besides each side's own toolchain (Rust through rustup, the system's clang, CMake, Ninja and GTK 3 and 4 with their development files, WebKitGTK 6 and GStreamer's for NSH): an `Xvfb`, `dbus-daemon`, `pmap` and GNU `dd`; `pidstat` (sysstat) for the switches. Here Xvfb 21.1.12 and sysstat 12.6.1 came from Ubuntu's packages unpacked into a private directory, and GTK 4's, WebKitGTK's and GStreamer's `.pc` files from their `-dev` packages the same way (`PKG_CONFIG_PATH`), no system change.

| | |
|---|---|
| `src/` | omui's side, package `omui.bench.competitors.apps`: `hello` (a title and a button), `counter` (the counter of tests/bench's `core/*` scenes), `market` (the live market board of `core/live-dashboard`), `list` (the 10,000 rows of `core/list-scroll`); the world is the package's feature (`full`, `app`, `lean`, `minimal`). `omui-app-cpu` is `app`'s hello told `--cpu` (no device made): the CPU window of the same program, which the GPU window's start is held to (UI9) and its rest compared with (red 154) |
| `slint/` | Slint 1.19.0's `hello`, `counter` and `list` (`Cargo.lock` pins the rest); its defaults: winit, FemtoVG over OpenGL, the fluent style |
| `flutter/` | Flutter 3.50.0's `hello`, `counter` and `list` (Material 3; `pubspec.lock`); the Linux runner is the one `flutter create` writes for that SDK, its window set to the side's size |
| `nsh/` | NSH's side where NSH has no app of its own for the work: `list`, the same 10,000 rows in its `List`. Its hello worlds are `nsh init`'s templates (`rust`, `ts`), its counter `examples/counter`, its board the gallery's Stocks (`NSH_GALLERY_DEMO=Stocks`), all at commit `83d5c4a5` (0.6.0) |

## How it is measured

- **The display.** An X server the harness starts (Xvfb 21.1.12, 1920 × 1080 × 24, no window manager, no compositor) and a session bus, as a desktop has. The harness is a client of the same server: DAMAGE tells it whenever anything is drawn on the screen, GetImage reads what is there, XTEST presses the buttons. A frame counts as presented when the server has carried out the drawing. **Or this machine's own display** (`--screen MONITOR`, RM1): its X server with its window manager and compositor, the windows opened on the named monitor; the harness authorises itself with the session's cookie and watches only each program's own window (found by its process: its damage, its pixels — GetImage on the window, never the screen), presses only while that window is under the pointer (kept above the person's windows for the case). The real pointer is the person's: it is borrowed only where a measurement cannot do without it — while a competitor's window opens (the window manager opens a new window on the pointer's monitor; an omui side is told where its window opens, `--position`, and the pointer is not touched for it), for the clicks (click → frame goes through the X server) and while the wheel turns over the list — and given back where the person had it as soon as that part is done (since 2026-11-27; it used to stay parked on the monitor for the whole run). A present is a run of non-overlapping draws on the window within half a refresh of each other, taken when its last draw lands (one image sent in strips is one present). Every side draws at scale 1 whatever the desktop's `Xft.dpi` (`GDK_DPI_SCALE`, `WINIT_X11_SCALE_FACTOR`); where a side still lays out otherwise there, its catalogue entry names its button and label on the desktop (`desktop`). Each ledger record says the display, what each side drew with and the machine's state (`display`, `conditions`).
- **Graphics.** GL reaches Mesa (25.0.7's llvmpipe with LLVM 20.1.7, software; the session's `LIBGL_DRIVERS_PATH`): NVIDIA's EGL makes no context on a server without DRI3, so every GL side (Slint's FemtoVG, Flutter's Impeller, NSH's GTK 4) draws on the CPU here. Vulkan reaches the RTX 4080 (omui's `--gpu` sides), presenting through the same server. On this machine's own display (`--screen`) every side's GL and Vulkan reach NVIDIA's driver; omui's programs link the toolchain's C library, whose loader does not search the system's, so they are given the driver the way the device tests are (`omui-devtools gpu --real`: xlings's `nvidia-gl-host-link` and Vulkan loader, SDL told the loader) — without it their GPU path finds no driver and the CPU draws. Each run records what its process mapped.
- **Start**: from launching the program to the first presented frame equal to the settled picture (nothing drawn for 0.6 s after it). Cold: the package's files and every file the process mapped on its previous run dropped from the page cache first (`dd iflag=nocache`, where no other process maps them); warm: launched again at once.
- **Memory at rest**, 5 s after that frame: `pmap -XX`'s totals of the process and its children — private dirty (what it wrote and alone holds), USS (private clean and dirty), PSS, RSS — and the process's by part (UI10, `memory_parts`): its mappings by what they are, the GPU driver's files and device nodes, the program, other libraries, the heap and the anonymous rest, each part's unique and private dirty bytes. A run with `--trace` holds each thread's trace buffer as well: its memory is not the program's.
- **At rest**, the next 5 s: processor time (mcpplibs.os), the threads' context switches a second (`pidstat -w -t`), frames drawn on the window; thread by thread (red 154), the first thread as `main` — a thread no one named carries the program's name, so a name alone does not say the main thread woke — and each thread's processor time (mcpplibs.os's `process::threads`: in the system's ticks, so a thread's share of a core over the 5 s is told to 0.2 %).
- **At rest, the pointer elsewhere** (red 158; the harness's own server only — on a desktop the pointer is its person's): the next 5 s with the pointer moved through XTEST in the corner it is parked in, outside the window, 120 steps a second; each thread's switches a second, as at rest. A program whose loop the server wakes for motion that is not its own shows it here.
- **Click**: the pointer on the button, pressed, everything settled, let go; from letting go to the first presented frame whose count (`label`) changed. Five a run.
- **In motion** (`market`, `list`): the same 5 s while it moves — the board by itself, the list with the wheel turned every 1/120 s — the frames presented a second, the intervals between them (p50, p95, p99, max), the processor, thread by thread.
- **Scrolled by its agent endpoint** (`--scroll-by agent`, red 155 and 160): an omui side's list is told `--agent-dir` and turned by its own endpoint instead of the X server's wheel — one `input` request a 1/120 s, a 40 px wheel notch where the window's middle is, sent without waiting for its answer — so the real pointer is not touched on a desktop (the other sides' lists, which have no endpoint, are left out of such a run). After the motion, 30 single notches, each once the list is still again: from sending the notch to the first frame presented after it (`notch_to_frame_ms`; the endpoint's input reaches the frame the loop runs next, as a device's would through the X server, so the time from a physical notch to the server is not in it). `omui-app-cpu`'s list is the same program told `--cpu`.
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

## Results on this machine's display (RM1)

2026-11-26 23:34 – 2026-11-27 00:14, 10 rounds, `omui-devtools bench apps --screen HDMI-0 --no-build --rounds 10 --only omui-app,slint,flutter,nsh-rust,nsh-ts`: the desktop's X server, window manager and compositor, every window on `HDMI-0` (2560 × 1440 at 120 Hz), every side's GL and Vulkan on the RTX 4080 (NVIDIA 580.178.04; each process mapped NVIDIA's driver). The processor's power preference `performance`; other sessions' builds ran throughout (1-minute load 1.8–15.3). The ledger's lines of that date with a `display` are this run's; the Xvfb tables above stay as the software path.

| side | app | start cold ms (p90) | warm ms | private dirty MiB | USS | PSS | RSS | CPU at rest % | switches/s | click → frame ms |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| omui-app | hello | 410 (501) | 355 | 20.1 | 71.0 | 75.9 | 100.3 | 0.40 | 113.8 | — |
| omui-app | counter | 405 (485) | 289 | 20.1 | 71.1 | 76.0 | 100.4 | 0.38 | 113.7 | 7.5 |
| slint | hello | 243 (276) | 251 | 18.3 | 33.8 | 39.3 | 70.6 | 0.00 | 2.0 | — |
| slint | counter | 252 (297) | 249 | 18.3 | 34.0 | 39.5 | 70.8 | 0.00 | 2.0 | 6.6 |
| flutter | hello | 508 (689) | 575 | 60.1 | 95.6 | 104.8 | 160.9 | 0.00 | 0.0 | — |
| flutter | counter | 549 (728) | 532 | 60.0 | 95.5 | 104.8 | 160.8 | 0.00 | 0.0 | — (†) |
| nsh-rust | hello | 250 (318) | 270 | 22.3 | 39.3 | 47.1 | 97.1 | 0.00 | 0.0 | 24.7 (‡) |
| nsh-ts | hello | 293 (393) | 252 | 27.8 | 58.6 | 66.7 | 121.4 | 0.00 | 0.0 | 31.5 |
| nsh-ts | counter | 253 (353) | 252 | 28.6 | 59.0 | 67.1 | 121.8 | 0.00 | 0.0 | 33.3 |

In motion, the same 5 s (present intervals over every round; the processor a frame is the rounds' median):

| side | app | frames/s | present p50 ms | p95 | p99 | CPU % | CPU ms a frame | private dirty MiB |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| omui-app | market | 60.0 | 16.66 | 17.32 | 25.00 | 19.4 | 3.2 | 31.5 |
| omui-app | list | 119.8 | 8.33 | 9.58 | 9.83 | 87.7 | 7.3 | 24.9 |
| slint | list | 62.4 | 16.67 | 17.07 | 18.27 | 9.5 | 1.5 | 21.1 |
| flutter | list | 30.0 | 32.84 | 36.23 | 45.06 | 60.8 | 16.4 | 88.0 (†) |
| nsh-ts | market | 30.3 | 32.24 | 38.76 | 40.42 | 28.5 | 8.2 | 91.4 |
| nsh-ts | list | 31.2 | 33.16 | 34.42 | 40.72 | 8.2 | 2.4 | 34.2 |

† Flutter draws its whole frame into the lower-left quarter of its window on this desktop (its picture, `target/bench-apps/shots/`): its click lands on nothing (not measured) and its start is to that picture — its rows are kept as recorded, not as Flutter's best. ‡ In some rounds a click changed nothing in NSH's Rust template's label (its layout on the desktop); not a ledger metric.

**The ledger's verdicts** on this display (`app` against the strongest; medians of the rounds, so a p95 here is the rounds' median p95):

| Metric | omui (`app`) | strongest | ratio [95 %] | verdict |
|---|---:|---|---|---|
| start, cold | 410 ms | Slint 243 | 0.58 [0.53, 0.64] | behind |
| start, warm | 355 ms | Slint 251 | 0.71 [0.64, 0.81] | behind |
| private dirty at rest | 20.1 MiB | Slint 18.3 | 0.91 [0.91, 0.91] | behind (NSH's published floor 41.6: within) |
| USS, PSS at rest (reported) | 71.0, 75.9 MiB | Slint 33.8, 39.3 | 0.48, 0.52 | behind |
| processor at rest | 0.40 % | Flutter, NSH 0 | 0 | behind |
| switches a second at rest (reported) | 113.8 | Flutter, NSH 0 | 0 | behind |
| click → frame | 7.5 ms | Slint 6.7 | 0.94 [0.51, 1.10] | behind |
| live board, present p95 | 17.0 ms | NSH 38.2 | 2.15 [1.85, 2.30] | ahead (not the same board) |
| live board, CPU a frame | 3.2 ms | NSH 8.2 | 2.12 [1.83, 2.85] | ahead (not the same board) |
| 10k list scrolled, present p95 | 8.7 ms | Slint 17.0 | 1.94 [1.81, 2.00] | ahead (Slint holds 60 Hz here) |
| 10k list scrolled, CPU a frame | 7.3 ms | Slint 1.5 | 0.24 [0.17, 0.29] | behind |
| programs, package | 17.21, 8.15 MB | Slint 17.47, 8.62 | 1.02, 1.06 | par |

What the real display shows that the software path did not:

- **The start was the GPU device's; since UI9 it is the CPU window's.** On the RTX 4080 `app` started in 410 ms cold and 355 warm (software GL with the CPU drawing: 324 and 84): the device was made before the first frame, and a warm start paid nearly all of it again — behind Slint (243, 251). Since UI9 (2026-11-27, [design](../../../../.agents/docs/2026-11-27-gpu-window-first-frame-design.md)) the window's first frames are the CPU's, the device is made after the first and takes over at rest without drawing the same picture again: 86.3 ms cold (p90 93.4) and 45.8 warm (p90 50.6), where the same program told `--cpu` (`omui-app-cpu`) takes 84.2 (86.1) and 43.1 (50.8) — 2.1 and 2.7 ms apart, within the frame (8.3 ms) the plan allows (10 rounds, 01:28–01:33, 1-minute load 1.5–3.1). The takeover holds the loop about 75 ms once, about 0.3 s after the start, at rest: SDL syncing the window with its manager (10 ms) and the driver making the swapchain.
- **Memory came down with the real driver.** 20.1 MiB private dirty (62.1 on llvmpipe): within NSH's published floor (41.6) and 0.91 of Slint's; USS is still twice Slint's (71.0 against 33.8).
- **At rest the GPU path's driver wakes; the program does not** (corrected 2026-11-27, red 154). 113.8 wakeups a second and 0.40 % of a core, where Flutter and NSH are at 0 and Slint at 0.005 — but the thread that wakes 100 times a second is not the main thread. NVIDIA's Vulkan driver starts it inside `vkCreateDevice` (on the device's making thread, between its `/dev/nvidiactl` calls; SDL_GPU asks for no more than `VK_KHR_swapchain`, `maintenance1` and `driver_properties`); it waits 10 ms at a time on a condition (`pthread_cond_timedwait` on the real-time clock) and signals another, and, named by no one, it carries the program's name. With `[vkrt]` (10 a second) and `[vkps]` (4) it is all the process does at rest: the main thread waits on the X connection with no timeout (strace: one `ppoll` from 0.9 s after the start to the end of the run). Thread by thread, 2026-11-27 01:28–01:33 (10 rounds, 1-minute load 1.5–3.1, power preference `performance`): the main thread 0 wakeups in 9 rounds of 10 (2.8 a second once) and no processor time in any; the driver's unnamed thread 99.0–99.8 a second, 0.2–0.4 % of a core; `[vkps]` 4.0–4.4, 0–0.2 %; `[vkrt]` 10.0; the process 113.4 a second and 0.43 %. The same program told to draw with the CPU (`omui-app-cpu`: `--cpu`, no device made) woke 0 times at 0.00 % in 10 rounds of 10. Both windows' main threads wake when the pointer moves anywhere on the desktop — SDL3's X11 driver selects XInput2's raw motion and buttons on the root window at start (`SDL_x11xinput2.c`) —, so rounds measured while the desktop is in use show them at 4–105 a second with nothing drawn (00:57–01:21, 2026-11-27), the GPU window and the CPU window alike. What remains is the driver's while a device exists: a window that makes no device has none of it (UI10).
- **The list keeps 120 Hz; the processor pays for it.** 119.8 frames a second, p95 9.6 ms, at 7.3 ms of CPU a frame (87.7 % of a core); Slint holds 60 Hz on this 120 Hz monitor at 1.5 ms a frame, NSH and Flutter 30. The p99 (9.8 ms) is over D-187's 8.3 ms.
- **The live board asks for 60.** omui's board presents at 60 frames a second on the 120 Hz monitor (its demand, D-70), p99 25.0 ms; NSH's Stocks at 30.

## The list scrolled by its agent endpoint (red 155, 160)

2026-11-27 06:37–07:12, `bench apps --screen HDMI-0 --scroll-by agent --dry-run`: omui's list (1896 × 1030) on this machine's display (RTX 4080, HDMI-0 at 120 Hz), the window placed by the program, the list turned a 40 px notch every 1/120 s by its own endpoint; the real pointer was not touched. Other sessions built throughout (1-minute load 2.0–14.0). One run of 5 rounds alternated six sides of the same program: as it was built before (2d25af71) and as it is now, on the GPU and with `--cpu`, and three ways of presenting on the GPU (variables a build of the experiment read, since taken out). Medians of the rounds; the intervals of every round together; a refresh missed is an interval of 1.5 refreshes or more.

| side | frames/s | CPU ms a frame (main, other threads) | other threads' switches a frame | interval p1 / p50 / p95 / p99 / max ms | refreshes missed | notch → frame ms (p50, p95) |
|---|---:|---|---:|---|---:|---|
| GPU, before | 119.8 | 2.18 (2.09, 0.05) | 2.8 | 7.30 / 8.33 / 9.03 / 9.42 / 10.86 | 0 of 2,988 | 8.22, 9.01 |
| GPU, waited for in the present (as before) | 119.8 | 2.01 (1.94, 0.05) | 2.7 | 6.85 / 8.33 / 9.62 / 9.81 / 9.96 | 0 of 2,989 | 8.21, 9.47 |
| GPU, the swapchain waited for before the frame's input | 119.8 | 2.38 (2.30, 0.05) | 2.7 | 6.99 / 8.33 / 9.48 / 9.82 / 11.09 | 0 of 2,990 | 8.17, 8.64 |
| the same, one frame in flight | 119.6 | 3.13 (3.04, 0.07) | 2.7 | 7.22 / 8.34 / 9.20 / 9.43 / 16.63 | 1 of 2,987 | 8.20, 9.98 |
| `--cpu`, before | 119.4 | 13.38 (5.90, 7.24) | 23.0 | 4.11 / 8.32 / 11.71 / 14.10 / 18.92 | 80 of 2,959 | 3.91, 4.69 |
| `--cpu`, now (red 160) | 120.0 | 10.53 (6.37, 4.05) | 6.0 | 4.40 / 8.69 / 11.72 / 13.85 / 21.50 | 73 of 2,835 | 4.28, 5.31 |

Separate runs of 3 rounds each the same morning: mailbox presenting (the loop holding itself to the refresh) missed refreshes in every round, max 16.7–25.0 ms (with one frame in flight 24.9–33.7); immediate presenting brought a notch to its frame in 3.6–3.9 ms (8.1–8.2 waiting for the refresh) at p99 11.0–11.7 ms and max 15.4–27.9. Before that run, with less load (3.2–8.0), `--cpu` before and now: 8.8–9.5 and 6.2–6.3 ms of CPU a frame, the other threads' switches 18 and 5.8 a frame.

What it shows:

- **On the GPU the list misses no refresh.** Every way of presenting kept 120 frames a second and missed at most one refresh in about 3,000; the intervals' spread (p1 7.3, p99 9.4 ms around 8.33) is when the X server carried each present out, early and late alike. The long gaps a profile saw inside the process (waits on the swapchain of 12–33 ms, red 155) do not show from outside in these runs, which recorded no trace. Waiting for the swapchain before the frame, or one frame in flight, changed nothing measurable — the frame's CPU, the spread, a notch's way to the screen — and was not kept; mailbox and immediate are worse at the tail. A notch reaches the screen in one refresh (8.2 ms) on the GPU, and in 3.1–4.3 ms on the CPU, whose frame is shown at once.
- **The CPU window's pool no longer looks for work** (red 160): its other threads take 4.1 ms a frame where they took 7.2, and switch 6 times a frame where they switched 23. The CPU window still misses refreshes at this size (73–80 in 3,000): sending 7.8 MB a frame to the X server is UI10's reason a window this large begins its device at its first frame.

## What the runs found in omui

- **The package does not start on this machine's system.** mcpp's toolchain links its own C library (glibc 2.44), so the packed program asks for `GLIBC_2.43`; Ubuntu 24.04 has 2.39 (`version 'GLIBC_2.43' not found`). A self-contained package (`--mode self-contained`, the C library carried) is 52 MB; a build against the system's (`--sysroot glibc@system`) fails in SDL3 (`posix_spawn_file_actions_addchdir` undeclared). What runs here is the same program as mcpp built it, beside the package's fonts — owed: a package that starts on the oldest supported system.
- **`app` and `full` load every Vulkan driver at start, though the window draws on the CPU.** The GPU path's provider opens the Vulkan loader, which loads each installed driver (here NVIDIA's, Mesa's RADV and lavapipe with LLVM 22 and ICU): 89 libraries at start against `lean`'s 46 (`LD_DEBUG=files`). `app` starts in 324 ms cold and 84 warm against `lean`'s 199 and 59; how much of that the drivers take is not measured apart.
- **`app` and `full` wake ten times a second at rest** (`lean` and `minimal` 0.4): Slint 2, Flutter and NSH 0. The processor is still about 0.03 % of a core.
- **Without GL a window is far cheaper.** `minimal` presents its CPU frames with SDL's software path (shared memory, no GL): 14.7 MiB private dirty and a cold start near 40 ms. Every GL path here is Mesa's llvmpipe, whose context is most of the other sides' private dirty memory.
- **`--gpu` does not start on this display.** With the RTX 4080 alone SDL_GPU finds no backend for an X server without DRI3 (Vulkan's own vkcube presents there); with lavapipe listed too it faults inside LLVM. The GPU rows are unverified here (D-201).

## Not measured here, and why

- On Xvfb, a real GPU's GL: NVIDIA's EGL needs DRI3 and Xvfb has none, so every GL side draws with llvmpipe there and those frame rows say what each does on a CPU. Since 2026-11-26 this machine's own display is measured too (`--screen`, the section above): the RTX 4080 and 120 Hz.
- On Xvfb, 120 Hz: it presents GL at 60 Hz, so a side that waits for the display shows about 16.7 ms between frames whatever it could do.
- System V shared memory is not available in this machine's sandbox (MIT-SHM attach fails): a window drawn on the CPU presents slower here than on a desktop with it. Every side of the display's run above drew with the GPU.
- Input from a device: the clicks are XTEST's, from the server; the time from a physical press to the server is the same for every side and not in the number.
- NSH's own timers (engine start, its first frame, event to commit) are inside its process; the start and the click here are the consensus' (§2a.1), from outside. Hot reload (C8) and the Web (C3) are not in this run.
- mtest's monitoring measures a case's own process (its processor time and largest resident set); another process's PSS, USS and private dirty, its context switches and its window's frames are not in it, so the harness reads them itself (`pmap`, `pidstat`, the X server). The first two are a general gap for mcpplibs.os (a program's memory accounting and switches) and mtest upstream, and so is a thread's processor time finer than the system's tick (`schedstat`'s nanoseconds), which a thread at rest needs.
