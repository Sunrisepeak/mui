# Competitors' side of the benchmarks

The other side of tests/bench's comparisons (plan §10.4: a lead is claimed per metric, against the best, measured). Each script does the work a named `omui-bench` benchmark does, the competitor pinned in `package.json`.

```sh
tests/bench/competitors/run.sh     # builds hanim's side natively and for the Web, installs the pinned competitors, runs each three times
```

hanim's side is `omui.bench.web` (tests/bench/web): motion/drive-1000's loop as a program of its own, so the same code runs natively and as WebAssembly — the Web's comparison in the same node, so the same V8 as the competitor.

## Interactive motion: 1000 values a frame

`gsap-1000.mjs` ↔ `motion/drive-1000`: 1000 values, every one retargeted every 30th frame, advanced one 60 Hz frame at a time, every value read. hanim: driven channels of an Engine on springs (`Drive::spring_to`, `Engine::advance`, `evaluate_driven`). GSAP has no spring: a tween with an overshooting ease (`elastic.out(1, 0.3)`, 0.5 s, `overwrite`), `gsap.updateRoot` per frame, its own ticker asleep.

| 2026-11-07, one machine (32 threads, Linux), three runs each | mean | p50 | p95 | max |
|---|---:|---:|---:|---:|
| hanim, WebAssembly (emscripten, release) in node 26.7.0 | 0.028 ms | 0.020 ms | 0.025 ms | 1.5 ms |
| GSAP 3.15.1 in the same node (V8; after 300 warm-up frames) | 0.135 ms | 0.042 ms | 0.058 ms | 3.7 ms |
| hanim, native (release, GCC 16) | 0.018 ms | 0.011 ms | 0.013–0.030 ms | 1.5 ms |

What it says: on the Web, in the same JavaScript engine, hanim's interactive mode does this work about 4.8× faster on average and 2× at the median and p95 than GSAP, and its worst frame (the one that retargets all 1000) is under half of GSAP's (1000 new tweens: ~3.6 ms; hanim's retarget keeps position and velocity in place). Natively it is a further 1.6× faster.

What it does not say: a browser's frame (node has no page, layout or paint; both sides write numbers, neither styles an element) — a page-level comparison, both driving 1000 elements' transforms, is next. Springs and an elastic ease are not the same curve; the work per value (a closed-form spring against an ease) is of the same order.
