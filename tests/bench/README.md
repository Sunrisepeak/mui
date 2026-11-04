# openmui.bench

The framework's benchmarks, and the gate made of them. `omui-bench` runs
them, prints a table, writes the report as JSON (`openmui.bench@0.1`,
`<name>.bench.omui.json`) and checks it against `budgets.bench.omui.json`:

```text
mcpp run -p openmui.bench --profile release -- --check tests/bench/budgets.bench.omui.json --json results.bench.omui.json
mcpp run -p openmui.bench --profile release -- --filter ui/frame     # some of them
mcpp run -p openmui.bench -- --list                                  # what there is
```

| Benchmark | What it measures |
|---|---|
| `ui/frame/one-label` | **M1 ⑤**: one value of the 1000-node dashboard changes; layout, paint and the CPU raster at 1920×1080 |
| `ui/frame/all-labels` | every value changes in one frame |
| `ui/frame/animate-all` | every card moves at once: the whole frame redrawn, in parallel bands |
| `ui/frame/hover-sweep` | the pointer moves from button to button: hit testing, hover restyle, repaint |
| `ui/frame/idle` | a frame with nothing to do (and its allocations: none) |
| `ui/layout/resize` | the window's width changes: layout alone |
| `ui/raster/full-1080p` | the whole display list through the CPU renderer, on one thread |
| `ui/raster/full-1080p-parallel` | the same in bands of 64 rows on the task pool — the same bytes |
| `ui/mount` | building, mounting and laying out the dashboard |
| `agent/snapshot` | the semantic snapshot an agent receives, as JSON |
| `text/paragraph` | shaping and breaking 2 KB of text (per byte) |
| `trace/span-off`, `trace/span-on` | a span while nothing records, and while a profile does (per span) |
| `log/filtered-out` | a debug record the filter leaves out (per record) |
| `canvas/pan-1000`, `canvas/zoom-sweep-1000` | a wall of 1000 windows panned by a trackpad, zoomed in and out by the wheel: the visible set, tiers, the display list |
| `canvas/pan-100k` | 100,000 windows, a few hundred on screen: the grid index makes it cost what is visible |
| `canvas/sharpen` | 40 windows of content, the camera stops at a new zoom: until every texture is rasterised at it |
| `wall/zoom-sweep-1000`, `wall/pan-1000` | S1's wall of 1000 pages: the wheel sweeping 1% ↔ 400%, a trackpad panning (the pool's work waited for outside the timed frame) |
| `wall/workspace-drag-100`, `wall/selection-drag-1000` | moving many windows at once: a workspace of 100 by its label, 1000 chosen ones by a title |
| `wall/stream-20` | 20 windows written into at 50 tokens a second |
| `gongfu/frame` | one frame of the fighting game's simulation, two scripted players (and its allocations: none) |
| `gongfu/rollback-8` | eight frames rolled back: the state restored, the frames simulated again, each saved (per frame) |
| `breakout/step` | a step of the widget breakout's simulation over a full wall (many balls swept among 96 bricks, capsules) |
| `breakout/seek` | a rewind of the widget breakout: the timeline seeking 0.5–30 s back |
| `gongfu/present2d` | one frame's picture built as a display list by a `Painter`: the stage, two skeletons, effects (allocations: none) |
| `wall/sharpen` | the camera stops over the wall: until every window on screen is sharp |
| `canvas-kit/drag-100k` | 10 chosen windows of 100,000 dragged every frame with select, drag, snap, grid and minimap on |

Each result has min, mean, p50, p95, p99 and max per iteration (nearest
rank), the cost per item, heap allocations per iteration (the program counts
them by replacing `operator new`), and the counters the body reports — node
counts and the runtime's phase times for the frame benchmarks.

A budget names a benchmark, a metric (`p95_ms`, `ns_per_item`,
`max_allocations`, a counter …), its limit, and why. Budgets are for release
builds; CI's release row checks them and keeps the report as an artifact.

Writing one:

```cpp
omui::bench::add("ui/frame/mine", "what it measures", [](omui::bench::State& s) {
    Scene scene;                       // setup: not timed
    while (s.next()) {                 // one iteration per pass
        scene.change();
        s.counter("nodes", scene.step().stats.nodes);
    }
});
```

Depends on `openmui` (the runtime, the base components, the headless host),
`openmui.trace` and `mcpplibs.os`.


## Public geometry and material plans

These six cases use the same harness, allocation counter, JSON/Markdown
reports and CLI as the existing benchmarks. Their implementation imports only
public `hanim.core`, `hanim.object`, geometry, raster and vector APIs; it does not call UI Runtime,
components or website builders. Preparation and steady-state work are distinct.
All six measure **CPU wall-clock work**. They neither submit GPU work nor
measure GPU execution, and say so in each result's notes.

```text
mcpp run -p openmui.bench --profile release -- --filter public-motion/ --json public-motion.bench.omui.json
```

| Case (`public-motion/` prefix) | One iteration / reported item |
|---|---|
| `object-query-500` | Capture 500 independently registered two-part objects, resolve each outline by name/ref, view its owned contour and check its capability. Preparation excluded. One item = one complete object's query batch; zero steady-state allocations are a budget. This does not evaluate an animation or render a frame. |
| `shape-prepare-star32` | Prepare and destroy one 32-edge concave star → four-cubic circle correspondence, using 64 arc samples and 32 alignment samples. Allocations are preparation costs, not forbidden frame allocations. One item = one plan. |
| `shape-sample-500` | Sample all prepared cubic segments of 500 differently phased objects through `sample_segment` into preallocated buffers; consume every coordinate in a checksum pass. Preparation/output allocation excluded. One item = one cubic sample, including its output write/checksum share; the actual cubic count is reported. |
| `paint-prepare-16` | Flatten, deduplicate and deep-own one material plan between 8 solids and 8 distinct two-stop gradients, then destroy it. One item = one of its 16 distinct leaves. No shader execution. |
| `paint-sample-record-500` | For 500 objects, call `sample` into a caller buffer and separately `record` into a reused DisplayList arena. Record samples again: 1000 API calls and 16,000 term writes per iteration, including owned gradient-stop copies. One item = one object doing both APIs. No drawing. |
| `draw-cpu-256` | Sample triangle→circle, record its current geometry and a gradient/normal-lit-image mixture, emit exactly one FillPath, clear and rasterize the 256×256 frame through CpuRenderer. Plan/resource preparation excluded. One item = one canvas pixel, including uncovered pixels. |

The last case includes current geometry preparation *sampling*, DisplayList
recording, leaf shader setup, once-only path coverage, CPU shading and
compositing. It reports real renderer allocation counts; those are not assumed
to be zero. It is neither a GPU benchmark nor a claim that 500 fully rendered
objects fit a frame. These different workloads must not be summed as if they
were measurements of one complete scene.

### Verified allocation gates; latency limits pending

The existing `budgets.bench.omui.json` limits remain unchanged. Two new
`max_allocations = 0` entries cover only the warmed shape sampling and material
sampling/arena recording workloads. The official release run confirmed both
at zero, including all measured iterations. Preparation and CPU drawing have
no zero-allocation gate. New latency limits remain pending an explicit frame
allocation and CI evidence; measured local speed alone does not define one.

The first official run (2026-11-04, Intel Core i9-13900K, GCC 16.1.0,
mcpp 2026.11.2.2, release, default three warm-ups and 400 ms target) measured:

| Case | Timed iterations | p95 per iteration (ms) | Mean / maximum heap allocations |
|---|---:|---:|---:|
| `shape-prepare-star32` | 6,973 | 0.060143 | 8 / 8 |
| `shape-sample-500` | 6,688 | 0.061022 | 0 / 0 |
| `paint-prepare-16` | 100,000 | 0.000363 | 11 / 11 |
| `paint-sample-record-500` | 2,049 | 0.197220 | 0 / 0 |
| `draw-cpu-256` | 268 | 1.796332 | 1.0112 / 2 |

The material preparation case reached the harness's 100,000-iteration cap
before 400 ms. These are local CPU wall-clock observations, with no core
pinning or physical GPU measurement. Re-running the sampling cases as part of
`--filter motion/` also retained zero allocations. That substring selects both
`public-motion/` and the five existing switch cases. Existing motion and
`ui/frame` budgets passed without changing their limits; the two GPU switch
cases used llvmpipe software Vulkan, so their existing `unless: software`
hardware-time exclusions applied.

The remaining acceptance work is:

| Cases | Proposed gate / evidence needed before adding the active entry |
|---|---|
| `shape-sample-500`, `paint-sample-record-500` | Active `max_allocations = 0` gates cover the exact warmed official workloads above. Continue enforcing them in CI; no allocation claim extends to preparation or rendering. |
| `shape-prepare-star32`, `paint-prepare-16` | Record `p95_ms`, `ns_per_item`, allocations and exact prepared/output counts. Preparation may allocate. Assign latency limits after measuring, against the first-transition CPU preparation work that must fit the chosen frame; do not gate preparation at zero allocations. |
| `shape-sample-500`, `paint-sample-record-500` | Record per-batch p95 plus per-cubic/per-object cost, respectively. D-94's 120 Hz objective gives **8.33 ms for an entire frame**, not 8.33 ms for each stage. Allocate stage limits only after measuring the corresponding end-to-end workload and its remaining frame budget. |
| `draw-cpu-256` | Record CPU p95/p99 and allocation counts as the reference-renderer workload. Choose an explicit preview/frame target and validate it on the named CPU before assigning a ceiling. Do not reuse hardware GPU limits for this case. |

Add only justified further limits to `budgets.bench.omui.json`, retaining the
report, machine/toolchain/profile, iteration configuration and reasoning
beside the checkpoint. Physical GPU
performance, GPU timestamps, larger object counts, pixel-domain morphs and the
complete Engine are separate acceptance work. Software GPU correctness/LSAN
results are not performance evidence for those items.
