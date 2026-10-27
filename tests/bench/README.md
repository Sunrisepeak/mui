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
