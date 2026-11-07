# Competitors' side of the benchmarks

The other side of tests/bench's comparisons (plan §10.4: a lead is claimed per metric, against the best, measured). Each script does the work a named `omui-bench` benchmark does, the competitor pinned in `package.json`.

```sh
mkdir -p target/bench-competitors && cp tests/bench/competitors/{package.json,*.mjs} target/bench-competitors/
(cd target/bench-competitors && npm install && node gsap-1000.mjs)
tools/dev/capped.sh mcpp run -p omui.bench --profile release -- --filter motion/drive-1000
```

## Interactive motion: 1000 values a frame

`gsap-1000.mjs` ↔ `motion/drive-1000`: 1000 values, every one retargeted every 30th frame, advanced one 60 Hz frame at a time, every value read. hanim: driven channels of an Engine on springs (`Drive::spring_to`, `Engine::advance`, `evaluate_driven`). GSAP has no spring: a tween with an overshooting ease (`elastic.out(1, 0.3)`, 0.5 s, `overwrite`), `gsap.updateRoot` per frame, its own ticker asleep.

| 2026-11-07, one machine (32 threads, Linux) | mean | p50 | p95 | max |
|---|---:|---:|---:|---:|
| hanim `motion/drive-1000` (native, release, GCC 16) | 0.019 ms | 0.010 ms | 0.013 ms | 1.08 ms |
| hanim `motion/set-1000` (hanim.core's Set alone) | 0.020 ms | 0.011 ms | 0.021 ms | 1.97 ms |
| GSAP 3.15.1 (node 26.7.0, V8, after 300 warm-up frames; three runs alike) | 0.137 ms | 0.043 ms | 0.060 ms | 3.7 ms |

What it says: on this work hanim's interactive mode is about 7× faster on average and 4× at the median; GSAP's cost is in the retargeting frames (1000 new tweens: ~3.6 ms), hanim's retarget keeps position and velocity in place (0.3 allocations a frame).

What it does not say yet: this is native C++ against JavaScript on V8. On the Web the fair comparison is hanim compiled to WebAssembly against GSAP in the same browser — next; until then no Web lead is claimed. Springs and an elastic ease are not the same curve; the work per value (a closed-form spring against an ease) is of the same order.
