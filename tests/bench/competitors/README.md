# omui.bench.competitors

Both sides of the comparisons with the best of each field (plan §10.4: a lead is claimed per metric, against the best, measured): the same work done by our engine and by the competitor, on one machine, with the method and the numbers kept here.

```sh
tests/bench/competitors/run.sh     # builds our side natively and for the Web, installs the pinned competitors, runs everything
```

| | |
|---|---|
| [src](src) | our side, package `omui.bench.competitors`: `omui-bench-motion` (hanim) and `omui-bench-vector` (xrender), programs that run natively and as WebAssembly |
| [js](js) | their side, pinned in `package.json`: GSAP, Skia (through @napi-rs/canvas), resvg |

## Interactive motion: 1000 values a frame

`js/gsap-1000.mjs` ↔ `src/motion.cpp`, motion/drive-1000's loop: 1000 values, every one retargeted every 30th frame, advanced one 60 Hz frame at a time, every value read. hanim: driven channels of an Engine on springs (`Drive::spring_to`, `Engine::advance`, `evaluate_driven`). GSAP has no spring: a tween with an overshooting ease (`elastic.out(1, 0.3)`, 0.5 s, `overwrite`), `gsap.updateRoot` per frame, its own ticker asleep.

| 2026-11-07, one machine (32 threads, Linux), three runs each | mean | p50 | p95 | max |
|---|---:|---:|---:|---:|
| hanim, WebAssembly (emscripten, release) in node 26.7.0 | 0.028 ms | 0.020 ms | 0.025 ms | 1.5 ms |
| GSAP 3.15.1 in the same node (V8; after 300 warm-up frames) | 0.135 ms | 0.042 ms | 0.058 ms | 3.7 ms |
| hanim, native (release, GCC 16) | 0.018 ms | 0.011 ms | 0.013–0.030 ms | 1.5 ms |

What it says: on the Web, in the same JavaScript engine, hanim's interactive mode does this work about 4.8× faster on average and 2× at the median and p95 than GSAP, and its worst frame (the one that retargets all 1000) is under half of GSAP's (1000 new tweens: ~3.6 ms; hanim's retarget keeps position and velocity in place). Natively it is a further 1.6× faster.

What it does not say: a browser's frame (node has no page, layout or paint; both sides write numbers, neither styles an element) — a page-level comparison, both driving 1000 elements' transforms, is next. Springs and an elastic ease are not the same curve; the work per value (a closed-form spring against an ease) is of the same order.

## Vector raster: the same paths, three CPU renderers

`src/vector.cpp` makes five scenes from seeds and writes each as SVG and as canvas calls; xrender's CPU renderer draws the display list, Skia's raster draws the canvas calls (@napi-rs/canvas 1.1.0: one call a path, the Path2D objects made before the clock), resvg 2.6.2 (tiny-skia) renders the SVG (parsed before the clock). 1024×1024, one thread each, the median of 15 frames after 3; then each picture against Skia's: the mean difference per channel and the pixels any channel of which is more than 8 of 255 away.

| 2026-11-07, one machine | xrender | Skia | resvg | xrender against Skia | resvg against Skia |
|---|---:|---:|---:|---|---|
| `fills-1000`: 1000 translucent blobs (6 cubics, radius 10–90) | 17.9 ms | **10.9** | 26.1 | 0.34, 0.68 % | 0.63, 0.81 % |
| `gradients-200`: 200 blobs, two-stop linear gradients | 16.6 | **7.1** | 19.9 | 0.28, 0.89 % | 0.27, 0.90 % |
| `small-5000`: 5000 opaque triangles of 2–8 px | **3.5** | 7.1 | 7.0 | 0.19, 1.43 % | 0.26, 2.19 % |
| `strokes-500`: open cubics 1–12 px wide, round caps and joins | 29.2 | **10.1** | 27.7 | 0.92, 7.26 % | 0.74, 5.11 % |
| `joins-300`: polylines with sharp corners, every join and cap, a third closed | 27.6 | **10.2** | 34.9 | 0.38, 1.25 % | 0.60, 1.98 % |

What it says: in quality xrender's CPU reference is as close to Skia as resvg is (closer on four scenes of five); in throughput it leads only where draws are many and small (2× Skia, whose figure includes a JavaScript call a path), and is 1.6–2.9× slower than Skia on large fills, gradients and strokes — faster than resvg except on curved strokes, where the two are level. No lead in raster throughput is claimed. The stroke rows are after this comparison's first finding: xrender's stroker made a polygon for every flattened segment and every join between them (94 ms for `strokes-500`, 9× Skia); it now makes one loop a contour, joins on the outer side of a turn and the offsets' meeting point (or Skia's pivot) on the inner — 3.2× faster, as close to Skia as before. Where two edges of one shape fall in the same pixel (a contour doubling back on itself) xrender's coverage adds them where Skia's supersampling counts the pixel once: the largest differences left.

Next: where large fills spend their time in xrender's rasterizer (cells sorted per shape, 64-bit divisions per crossing) and its compositing; the same scenes on the GPU renderer beside Vello; the Web, as WebAssembly, beside Skia's CanvasKit.
