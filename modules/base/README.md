# modules/base

Foundations that know nothing of interfaces, graphics or games.

Depends on: the standard library and third-party libraries. Used by: every other layer.

| Package | |
|---|---|
| [hanim](hanim) | The animation engine: tweens, keyframes, analytic springs and decay, timelines, OKLab colour; every animation a pure function of time |
| [geom](geom) | 2D geometry: points, rectangles, colours, affine transforms, pixel snapping |
| [memory](memory) | Frame arena, generational handles, slot maps, `FunctionRef` |
| [unicode](unicode) | Line breaking (UAX #14) and segmentation (UAX #29) without ICU |
| [fuzzy](fuzzy) | Fuzzy matching and ranking with highlight positions |
| [trace](trace) | Spans, counters and marks; Chrome trace JSON; near-free while off |
| [session](session) | Recorded sessions (`*.session.omui.jsonl`): timestamped, seekable, mergeable |
| [pipeline](pipeline) | Pipelines of stage plugins over typed resources: checked, deterministic, traced, replaceable while running |
| [image](image) | Image buffers, premultiplication, PNG, animated PNG and WebP |
| [testing](testing) | Checks, suites and determinism hashes for test programs |

Start with [hanim](hanim/README.md).
