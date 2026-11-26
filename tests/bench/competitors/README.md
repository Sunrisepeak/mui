# omui.bench.competitors

Both sides of the comparisons with the best of each field (plan §10.4: a lead is claimed per metric, against the best, measured): the same work done by our engine and by the competitor, on one machine, with the method and the numbers kept here.

```sh
omui-devtools bench competitors                        # builds our side natively and for the Web, installs the pinned competitors, runs everything
omui-devtools bench competitors --ledger --rounds 15   # the lead ledger: 15 rounds, the sides alternating in one job, judged by ratio
```

**The lead ledger** (Part 2 plan §2, D-135). [`lead.metrics.xdef.json`](lead.metrics.xdef.json) is the reviewed list of every project's metrics — measured here, planned (no harness yet) or pending a device. `--ledger` runs our side and each competitor's alternately, round after round, in this one job; `omui-devtools lead` ([tools/devtools](../../../tools/devtools)) pairs them by round, takes the ratio a round (theirs ÷ ours where lower is better), its median and a seeded 95 % bootstrap interval, the strongest competitor (the lowest ratio), and judges from the interval's lower end: < 0.95 behind, < 1.2 par, < 10 ahead, else far ahead — an interval across a threshold takes the worse tier. Each verdict is a line of [`lead.ledger.xdef.jsonl`](../../../.agents/docs/evaluation/lead.ledger.xdef.jsonl), summarised in [`lead.md`](../../../.agents/docs/evaluation/lead.md). Only ratios measured side by side count; the absolute times below are one machine's. CI's Bench lane runs it every Monday (`.github/workflows/bench.yml`).

| | |
|---|---|
| [src](src) | our side, package `omui.bench.competitors`: `omui-bench-motion` (hanim) and `omui-bench-vector` (mrender), programs that run natively and as WebAssembly |
| [js](js) | their side, pinned in `package.json`: GSAP, Skia (through @napi-rs/canvas), resvg |
| [py](py) | their side, pinned in `requirements.txt`: ThorVG and rlottie, Lottie's native players (their C libraries through ctypes) |
| [apps](apps) | whole applications, both sides (`omui-devtools bench apps`): omui in each world beside Slint, Flutter and NSH — size, start, memory, rest, a click, a live board and a scrolled list, each in a real window and measured from outside the process (C1, N2) |

## Whole applications: omui beside Slint, Flutter and NSH

[apps](apps) (`omui-devtools bench apps`): the same hello, counter, live board and 10,000-row list, each built at its pin, launched in a window of the harness's own X server and measured from outside the process (2026-11-19, 10 rounds; GL is Mesa's llvmpipe here, so every side draws on the CPU). In the default world (`app`): the program and the package are level with Slint's (17.70 against 17.47 MB, 8.42 against 8.62 MB packed) and smaller than Flutter's and NSH's; the start (324 ms cold, 84 warm against Slint's 160 and 70), the memory at rest (62.1 against 49.7 MiB private dirty) and the scrolled list (49.7 against 17.3 ms p95 between frames) are behind; a click to the next frame is 2.0 ms (Slint 1.4, Flutter 16.5, NSH 21–26). `minimal` (no GL) starts in 46 ms with 14.7 MiB. What it found, the method and every number: [apps/README.md](apps/README.md).

## Interactive motion: 1000 values a frame

`js/gsap-1000.mjs` ↔ `src/motion.cpp`, motion/drive-1000's loop: 1000 values, every one retargeted every 30th frame, advanced one 60 Hz frame at a time, every value read. hanim: driven channels of an Engine on springs (`Drive::spring_to`, `Engine::advance`, `evaluate_driven`). GSAP has no spring: a tween with an overshooting ease (`elastic.out(1, 0.3)`, 0.5 s, `overwrite`), `gsap.updateRoot` per frame, its own ticker asleep.

| 2026-11-07, one machine (32 threads, Linux), three runs each | mean | p50 | p95 | max |
|---|---:|---:|---:|---:|
| hanim, WebAssembly (emscripten, release) in node 26.7.0 | 0.028 ms | 0.020 ms | 0.025 ms | 1.5 ms |
| GSAP 3.15.1 in the same node (V8; after 300 warm-up frames) | 0.135 ms | 0.042 ms | 0.058 ms | 3.7 ms |
| hanim, native (release, GCC 16) | 0.018 ms | 0.011 ms | 0.013–0.030 ms | 1.5 ms |

What it says: on the Web, in the same JavaScript engine, hanim's interactive mode does this work about 4.8× faster on average and 2× at the median and p95 than GSAP, and its worst frame (the one that retargets all 1000) is under half of GSAP's (1000 new tweens: ~3.6 ms; hanim's retarget keeps position and velocity in place). Natively it is a further 1.6× faster.

What it does not say: a browser's frame (node has no page, layout or paint; both sides write numbers, neither styles an element) — a page-level comparison, both driving 1000 elements' transforms, is next. Springs and an elastic ease are not the same curve; the work per value (a closed-form spring against an ease) is of the same order.

## Lottie playback: the same animation, three players

`src/lottie.cpp` ↔ `py/lottie.py`: 200 rounded rectangles, each moving through three places, turning, scaling and fading on the same cubic-bezier eases (easeInOutCubic), 2 s at 60 frames a second on a 512 × 512 stage — made from a seed and written as a Lottie file, which ThorVG and rlottie play; hanim plays the same values as its own objects and programs (the engine sought, the frame recorded as a display list, mrender's CPU renderer). Every frame is drawn whole on one thread, visited in a scrambled order (frame 37·k mod 120: a seek each time), the clock round the seek and the drawing; frame 60 of each is compared with hanim's (`omui-bench-lottie --compare`). `omui-bench-lottie --out DIR --phases` says where hanim's frame goes: the seek and evaluation, the display list recorded, mrender's drawing.

| 2026-11-16, one machine, 15 rounds alternating (medians of the rounds) | median frame | p95 | frame 60 against hanim's |
|---|---:|---:|---|
| hanim (release, GCC 16) | 1.072 ms | 1.124 | — |
| ThorVG 1.0.4 (software canvas, no worker threads) | **0.919 ms** | 0.953 | mean 0.40 a channel, 0.002 % of pixels > 16 off: the same picture |
| rlottie 1.3.8 | 39.8 ms | 40.0 | drawn without the turns and scales: not the same animation, not compared |

The calls through Python cost ThorVG 0.019 ms a frame (the same calls on one layer), counted in its time, not subtracted.

What it says: ThorVG plays this animation in 0.86× hanim's time (ledger: `playback.lottie-200.median_ms`, behind, [0.851, 0.867]). Where hanim's time goes (perf, 10 k samples): mrender's rasteriser — cells accumulated (scanline, line, band, sweep: ~36 %) and coverage composited (~27 %) — while hanim's evaluation (eases, the vector recording) is about a tenth. The gap is the same as the large fills' behind Skia below: the rasteriser's throughput, mrender's to close; hanim's own share is not where it lies. What it does not say: a GPU's frame (the same scene through mrender's GPU renderer is the comparison to make against ThorVG's WebGPU canvas next), or hanim's determinism and seeking, where a Lottie player has no equivalent to measure.

## Vector raster: the same paths, three CPU renderers

`src/vector.cpp` makes five scenes from seeds and writes each as SVG and as canvas calls; mrender's CPU renderer draws the display list, Skia's raster draws the canvas calls (@napi-rs/canvas 1.1.0: one call a path, the Path2D objects made before the clock), resvg 2.6.2 (tiny-skia) renders the SVG (parsed before the clock). 1024×1024, one thread each, the median of 15 frames after 3; then each picture against Skia's: the mean difference per channel and the pixels any channel of which is more than 8 of 255 away.

| 2026-11-07, one machine | mrender | Skia | resvg | mrender against Skia | resvg against Skia |
|---|---:|---:|---:|---|---|
| `fills-1000`: 1000 translucent blobs (6 cubics, radius 10–90) | 17.9 ms | **10.9** | 26.1 | 0.34, 0.68 % | 0.63, 0.81 % |
| `gradients-200`: 200 blobs, two-stop linear gradients | 16.6 | **7.1** | 19.9 | 0.28, 0.89 % | 0.27, 0.90 % |
| `small-5000`: 5000 opaque triangles of 2–8 px | **3.5** | 7.1 | 7.0 | 0.19, 1.43 % | 0.26, 2.19 % |
| `strokes-500`: open cubics 1–12 px wide, round caps and joins | 29.2 | **10.1** | 27.7 | 0.92, 7.26 % | 0.74, 5.11 % |
| `joins-300`: polylines with sharp corners, every join and cap, a third closed | 27.6 | **10.2** | 34.9 | 0.38, 1.25 % | 0.60, 1.98 % |

What it says: in quality mrender's CPU reference is as close to Skia as resvg is (closer on four scenes of five); in throughput it leads only where draws are many and small (2× Skia, whose figure includes a JavaScript call a path), and is 1.6–2.9× slower than Skia on large fills, gradients and strokes — faster than resvg except on curved strokes, where the two are level. No lead in raster throughput is claimed. The stroke rows are after this comparison's first finding: mrender's stroker made a polygon for every flattened segment and every join between them (94 ms for `strokes-500`, 9× Skia); it now makes one loop a contour, joins on the outer side of a turn and the offsets' meeting point (or Skia's pivot) on the inner — 3.2× faster, as close to Skia as before. Where two edges of one shape fall in the same pixel (a contour doubling back on itself) mrender's coverage adds them where Skia's supersampling counts the pixel once: the largest differences left.

**Against the picture itself** (2026-11-15, the release plan's RC2.8). Skia's 1× picture is an approximation too: its stroker offsets curves as quadratics within a tolerance, and its coverage and tiny-skia's (resvg's) are sampled — a renderer nearer the true shape is farther from Skia's. So each picture is also compared with `exact`: the scene drawn by Skia and by resvg at 8× the size, each box-filtered back over its 64 samples a pixel, the two averaged (kept beside the scene by its SVG's hash; `vector.mjs`). Skia's own 1× picture is then one of the compared, not the judge. How far the two 8× drawings are apart is printed too (`references_apart`): a margin between renderers smaller than that is not resolved, and such a scene is not a ledger metric.

| 2026-11-15, mean channel difference from `exact`, pixels > 8 off | references apart | mrender | resvg | Skia |
|---|---:|---|---|---|
| `fills-1000` | 0.522 | 0.252, 0.01 % | 0.328, 0.22 % | 0.379, 0.75 % |
| `gradients-200` | 0.143 | 0.147, 0.02 % | 0.171, 0.42 % | 0.303, 0.93 % |
| `small-5000` | 0.024 | **0.034**, 0.09 % | 0.187, 1.37 % | 0.197, 1.63 % |
| `strokes-500` | 0.150 | **0.238**, 0.62 % | 0.839, 6.78 % | 0.952, 7.95 % |
| `joins-300` | 0.308 | 0.255, 0.40 % | 0.390, 1.09 % | 0.423, 1.57 % |

What it says: mrender's coverage is exact area (FreeType's cells) on geometry flattened to 1/32 px, and where the reference resolves it that shows: on strokes it is 3.5× nearer the picture than resvg, with a tenth of the pixels off; on small triangles 5.5×. On fills, gradients and joins the two references are further apart than the renderers (the 8-bit compositing of hundreds of translucent layers rounds differently in Skia and tiny-skia, at any size): there no lead is claimed, only fewer pixels off. The `strokes-500.error` row above (behind resvg against Skia) measured closeness to Skia's approximation, which resvg — Skia's port — shares. What it does not hide: mrender's largest single differences are where a stroke turns tighter than its half-width (the inside of a hairpin): there the stroker's pivots overlap and the cells add what supersampling counts once — 73 pixels of the million more than 40 off on `strokes-500`, 71 of them darker. The fix is the stroker's (the inner side of a tight run collapsed to where its offsets cross), next to land with the pixel references it moves.

Next: where large fills spend their time in mrender's rasterizer (cells sorted per shape, 64-bit divisions per crossing) and its compositing; the same scenes on the GPU renderer beside Vello; the Web, as WebAssembly, beside Skia's CanvasKit.

## Markdown to HTML: 1 MiB of what people wrote here

`src/markdown.cpp` ↔ `js/markdown.mjs`: the repository's own Markdown (docs/, the site's content) repeated to 1 MiB, turned into HTML the whole way, in one node (V8). mlibs.markdown is built on md4c: its time is md4c's parse, the document of blocks and marks, and the HTML written from the document; md4w is md4c's own HTML writer as WebAssembly (with our gfm's flags; it also writes an anchor into each heading), markdown-it the most used JavaScript one.

| 2026-11-09, one machine, 10 interleaved rounds | median | throughput |
|---|---:|---:|
| mlibs.markdown, WebAssembly | 12.8 ms | 83 MB/s |
| md4w 0.2.7 (md4c's own HTML) | 7.6–8.9 ms | 120+ MB/s |
| markdown-it 15.0.3 | 69.7 ms | 15 MB/s |
| mlibs.markdown, native | 9.9 ms | 108 MB/s |

What it says: the document model costs: 0.60× md4c's straight-to-HTML (behind, in the ledger), 5.4× markdown-it. What the document buys — a renderer, an accessibility tree and an agent reading the same blocks, streaming append — is not in the number; making it cheaper is LD2's.

