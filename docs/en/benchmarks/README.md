# openmui benchmarks

Measured 2026-10-27T22:29:40Z (Linux x86_64, GCC 16.1, release; GPU: Vulkan through SDL_GPU). Every number is the wall-clock time of one iteration on the machine below; a GPU benchmark's includes submitting the frame and waiting for the GPU to finish it. `omui-bench --report` wrote this page from the same run as its JSON (`--json`).

## The machine

| | |
|---|---|
| processor | 13th Gen Intel(R) Core(TM) i9-13900K |
| hardware threads | 32 |
| architecture | x86_64 |
| memory | 62.5 GiB |
| system | Ubuntu 24.04.1 LTS, kernel 6.8.0-147-generic |
| GPU | NVIDIA GeForce RTX 4080 (vulkan) |

## Notes

- The plan holds S1 and S2 to 120 Hz on an integrated GPU (§19.3), on a reference machine of an 8-core CPU with a mainstream discrete GPU or a recent integrated one (§12.6). This machine's integrated GPU is not enabled, so the GPU rows are a discrete GPU's (the RTX 4080 above); an integrated GPU's numbers are for a later report.
- The GPU rows draw offscreen (SDL's offscreen video driver): each frame is encoded, drawn whole into a 1920×1080 texture and waited for; presenting to a display is not in them. `encode_ms` is the CPU's part, `uploaded_kb` what the frame sent to the device.
- CI runs this suite on every change (the Ubuntu release row: a four-core runner, lavapipe as its GPU) against the same budgets, and keeps its report as an artifact.

## The method

Each benchmark runs 3 warm-up iterations, then at least 12 timed ones and at least 400 ms of them (at most 100000). The percentiles are of the timed iterations, nearest rank. Time a body pauses (building its input) is not counted. Heap allocations are counted by the program's own `operator new`: the libraries measured are the ones every application links. Release profile; the CPU renderer uses the task pool (every hardware thread). A budget is a limit CI holds each change to (`--check`), with the reason it is there; one marked *unless software* is a GPU's, not held on a software device (lavapipe).

## The results

### canvas

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `canvas/sharpen` | 602 | 0.665 ms | 1.18 ms | 1.41 ms | 1.85 ms | 2259.0 | p95_ms ≤ 300 ms | ✓ |
| `canvas/pan-1000` | 25563 | 0.015 ms | 0.022 ms | 0.028 ms | 0.134 ms | 23.2 | p95_ms ≤ 1 ms | ✓ |
| `canvas/zoom-sweep-1000` | 23512 | 0.009 ms | 0.064 ms | 0.077 ms | 0.099 ms | 17.9 | p95_ms ≤ 1 ms | ✓ |
| `canvas/pan-100k` | 32120 | 0.013 ms | 0.014 ms | 0.016 ms | 0.028 ms | 21.9 | p95_ms ≤ 1 ms | ✓ |

- `canvas/sharpen`: 40 windows of content on screen, the camera stops at a new zoom: until every texture is rasterised at it (§3.5, < 300 ms).
- `canvas/pan-1000`: 1000 windows, a trackpad pans every frame: the visible set, tiers and the display list. `nodes` = 3. `laid_out` = 0. `painted` = 0. `layout_ms` = 1.60177e-05. `paint_ms` = 2.60485e-05. `runtime_ms` = 0.000293758. `display_commands` = 899.142.
- `canvas/zoom-sweep-1000`: 1000 windows, the wheel zooms in and out about the centre every frame. `nodes` = 3. `laid_out` = 0. `painted` = 0. `layout_ms` = 1.84381e-05. `paint_ms` = 2.58645e-05. `runtime_ms` = 0.000296615. `display_commands` = 721.374.
- `canvas/pan-100k`: 100,000 windows, a few hundred on screen: the frame follows what is visible, not the scene. `nodes` = 3. `laid_out` = 0. `painted` = 0. `layout_ms` = 1.69959e-05. `paint_ms` = 2.60757e-05. `runtime_ms` = 0.000297301. `display_commands` = 830.756.

### canvas-kit

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `canvas-kit/drag-100k` | 3050 | 0.115 ms | 0.251 ms | 0.274 ms | 0.285 ms | 21.5 | p95_ms ≤ 1 ms | ✓ |

- `canvas-kit/drag-100k`: 100,000 windows, 10 chosen dragged every frame with select, drag, snap, grid and minimap on. `nodes` = 3. `laid_out` = 0. `painted` = 3. `layout_ms` = 2.42902e-05. `paint_ms` = 0.123235. `runtime_ms` = 0.131007. `display_commands` = 1158.28.

### wall

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `wall/zoom-sweep-1000` | 8484 | 0.016 ms | 0.115 ms | 0.137 ms | 1.96 ms | 193.4 | p95_ms ≤ 1 ms | ✓ |
| `wall/zoom-sweep-1000-gpu` | 829 | 0.257 ms | 1.20 ms | 7.11 ms | 13.63 ms | 3609.8 | p99_ms ≤ 8.33 ms (unless software) | ✓ |
| `wall/pan-1000` | 36573 | 0.011 ms | 0.013 ms | 0.015 ms | 0.191 ms | 96.2 | p95_ms ≤ 1 ms | ✓ |
| `wall/workspace-drag-100` | 5636 | 0.044 ms | 0.231 ms | 0.242 ms | 0.299 ms | 667.2 | p95_ms ≤ 1 ms | ✓ |
| `wall/selection-drag-1000` | 2565 | 0.120 ms | 0.355 ms | 0.376 ms | 0.446 ms | 1945.6 | p95_ms ≤ 1 ms | ✓ |
| `wall/stream-20` | 5840 | 0.001 ms | 0.593 ms | 1.16 ms | 1.75 ms | 1113.5 | p95_ms ≤ 2 ms | ✓ |
| `wall/sharpen` | 12 | 17.11 ms | 53.31 ms | 53.31 ms | 53.31 ms | 980705.6 | p95_ms ≤ 300 ms | ✓ |

- `wall/zoom-sweep-1000`: 1000 windows, the wheel sweeps 1% ↔ 400% about the centre: tiers, textures asked for and stretched. `visible` = 380.836. `textured` = 65.3675. `stretched` = 0.0792079. `live` = 0. `canvas_paint_ms` = 0.0229718. `nodes` = 4. `laid_out` = 0. `painted` = 4. `layout_ms` = 2.17381e-05. `paint_ms` = 0.0320678. `runtime_ms` = 0.0469179.
- `wall/zoom-sweep-1000-gpu`: the same sweep with the GPU drawing every frame (S1: 120 Hz, frame p99 under 8.3 ms). `visible` = 381.154. `textured` = 63.9324. `stretched` = 0.810615. `live` = 0. `canvas_paint_ms` = 0.0768265. `nodes` = 4. `laid_out` = 0. `painted` = 4. `layout_ms` = 3.47636e-05. `paint_ms` = 0.0898003. `runtime_ms` = 0.115634. `encode_ms` = 0.204143. `uploaded_kb` = 281.009. `gpu_ms` = 0.366314. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).
- `wall/pan-1000`: 1000 windows as textures (about 160 px each), a trackpad pans every frame. `visible` = 58.8754. `textured` = 54.2375. `stretched` = 0. `live` = 0. `canvas_paint_ms` = 0.0053668. `nodes` = 4. `laid_out` = 0. `painted` = 4. `layout_ms` = 1.92902e-05. `paint_ms` = 0.00687759. `runtime_ms` = 0.0107505.
- `wall/workspace-drag-100`: a workspace of 100 windows dragged by its label every frame (a declarative behaviour moving 101 nodes). `visible` = 142. `textured` = 140. `stretched` = 0. `live` = 0. `canvas_paint_ms` = 0.0116792. `nodes` = 4. `laid_out` = 0. `painted` = 4. `layout_ms` = 2.05506e-05. `paint_ms` = 0.0497577. `runtime_ms` = 0.0707273.
- `wall/selection-drag-1000`: all 1000 windows chosen, dragged by one title bar every frame (canvas-kit's drag and snap). `visible` = 15.2074. `textured` = 14.2047. `stretched` = 0. `live` = 0. `canvas_paint_ms` = 0.00155712. `nodes` = 4. `laid_out` = 0. `painted` = 4. `layout_ms` = 2.32339e-05. `paint_ms` = 0.0881079. `runtime_ms` = 0.15574.
- `wall/stream-20`: 20 windows written into at 50 tokens a second each: live ones append, textures stale four times a second. `visible` = 22. `textured` = 20. `stretched` = 1.19897. `live` = 0. `canvas_paint_ms` = 0.0394586. `nodes` = 4. `laid_out` = 0. `painted` = 0.532877. `layout_ms` = 3.61613e-05. `paint_ms` = 0.0565373. `runtime_ms` = 0.0678359.
- `wall/sharpen`: the camera stops at a new zoom over a wall of 1000 pages: until every window on screen is sharp (§3.5, < 300 ms).

### glass

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `glass/10-1080p-cpu` | 12 | 49.57 ms | 60.65 ms | 60.65 ms | 60.65 ms | 1591.0 | p95_ms ≤ 250 ms | ✓ |
| `glass/10-1080p-gpu` | 352 | 1.07 ms | 1.61 ms | 2.09 ms | 4.37 ms | 44.0 | p95_ms ≤ 8.33 ms (unless software) | ✓ |

- `glass/10-1080p-cpu`: ten liquid glass panels at 1920×1080, the frame drawn whole by the CPU reference (on the task pool, in bands).
- `glass/10-1080p-gpu`: the same frame on the GPU (omui.rhi), submitted and waited for. `encode_ms` = 0.520118. `uploaded_kb` = 9.5625. `gpu_ms` = 1.13909. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).

### styles

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `styles/glass-gpu` | 491 | 0.685 ms | 1.38 ms | 2.05 ms | 3.59 ms | 237.8 | p95_ms ≤ 8.33 ms (unless software) | ✓ |
| `styles/neon-gpu` | 519 | 0.749 ms | 0.882 ms | 1.60 ms | 2.17 ms | 501.4 | p95_ms ≤ 8.33 ms (unless software) | ✓ |
| `styles/retro-gpu` | 1381 | 0.282 ms | 0.363 ms | 0.567 ms | 1.97 ms | 195.8 | p95_ms ≤ 8.33 ms (unless software) | ✓ |
| `styles/ink-gpu` | 718 | 0.523 ms | 0.734 ms | 1.40 ms | 1.81 ms | 316.4 | p95_ms ≤ 8.33 ms (unless software) | ✓ |
| `styles/liquid-gpu` | 1407 | 0.253 ms | 0.401 ms | 0.525 ms | 3.34 ms | 250.7 | p95_ms ≤ 8.33 ms (unless software) | ✓ |

- `styles/glass-gpu`: S2's interface in the glass pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole. `nodes` = 70. `laid_out` = 26. `painted` = 66. `layout_ms` = 0.0231492. `paint_ms` = 0.0355784. `runtime_ms` = 0.066081. `encode_ms` = 0.365388. `uploaded_kb` = 104.137. `gpu_ms` = 0.743662. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).
- `styles/neon-gpu`: S2's interface in the neon pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole. `nodes` = 103. `laid_out` = 26. `painted` = 99. `layout_ms` = 0.00868154. `paint_ms` = 0.0217288. `runtime_ms` = 0.0339286. `encode_ms` = 0.3042. `uploaded_kb` = 144.623. `gpu_ms` = 0.735185. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).
- `styles/retro-gpu`: S2's interface in the retro pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole. `nodes` = 68. `laid_out` = 26. `painted` = 64. `layout_ms` = 0.00997256. `paint_ms` = 0.0138663. `runtime_ms` = 0.0265156. `encode_ms` = 0.0910595. `uploaded_kb` = 103.695. `gpu_ms` = 0.261972. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).
- `styles/ink-gpu`: S2's interface in the ink pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole. `nodes` = 80. `laid_out` = 26. `painted` = 76. `layout_ms` = 0.00897407. `paint_ms` = 0.0160457. `runtime_ms` = 0.0278862. `encode_ms` = 0.164047. `uploaded_kb` = 114.742. `gpu_ms` = 0.527985. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).
- `styles/liquid-gpu`: S2's interface in the liquid pack at 1920×1080, the slider moving: the runtime's frame, then the GPU draws it whole. `nodes` = 68. `laid_out` = 26. `painted` = 64. `layout_ms` = 0.00657941. `paint_ms` = 0.0100328. `runtime_ms` = 0.0186292. `encode_ms` = 0.0847631. `uploaded_kb` = 101.716. `gpu_ms` = 0.264492. `software` = 0. device: NVIDIA GeForce RTX 4080 (vulkan).

### ui

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `ui/frame/idle` | 2789 | 0.143 ms | 0.151 ms | 0.156 ms | 0.243 ms | 0.0 | p95_ms ≤ 0.5 ms<br>max_allocations ≤ 0 | ✓<br>✓ |
| `ui/frame/one-label` | 283 | 1.31 ms | 2.08 ms | 3.18 ms | 3.62 ms | 12544.3 | p95_ms ≤ 16.67 ms | ✓ |
| `ui/frame/all-labels` | 130 | 3.03 ms | 3.79 ms | 4.01 ms | 4.05 ms | 48761.8 | p95_ms ≤ 16.67 ms | ✓ |
| `ui/frame/animate-all` | 210 | 1.80 ms | 2.61 ms | 3.11 ms | 4.44 ms | 69.9 | p95_ms ≤ 16.67 ms | ✓ |
| `ui/frame/hover-sweep` | 474 | 0.905 ms | 0.933 ms | 0.948 ms | 0.991 ms | 10.6 | p95_ms ≤ 16.67 ms | ✓ |
| `ui/layout/resize` | 5236 | 0.077 ms | 0.080 ms | 0.084 ms | 0.106 ms | 1089.0 |  |  |
| `ui/raster/full-1080p` | 73 | 5.49 ms | 5.54 ms | 5.57 ms | 5.57 ms | 4.0 |  |  |
| `ui/raster/full-1080p-parallel` | 417 | 0.937 ms | 1.13 ms | 1.37 ms | 1.45 ms | 45.0 |  |  |
| `ui/mount` | 64 | 6.28 ms | 6.36 ms | 6.44 ms | 6.44 ms | 146180.0 | p95_ms ≤ 16.67 ms | ✓ |

- `ui/frame/idle`: nothing changed: the frame the runtime skips. `nodes` = 1012. `laid_out` = 0. `painted` = 0. `layout_ms` = 1.59652e-05. `paint_ms` = 3.17121e-05. `runtime_ms` = 0.000338319.
- `ui/frame/one-label`: M1 ⑤: one card's value changes; relayout, repaint and raster at 1920x1080. `nodes` = 1012. `laid_out` = 2042. `painted` = 1012. `layout_ms` = 0.631354. `paint_ms` = 0.110155. `runtime_ms` = 0.767264.
- `ui/frame/all-labels`: every card's value changes at once (the worst ordinary frame). `nodes` = 1012. `laid_out` = 3703. `painted` = 1012. `layout_ms` = 1.8239. `paint_ms` = 0.131831. `runtime_ms` = 1.98278.
- `ui/frame/animate-all`: every card moves at once (a transition on all 168): the whole frame is redrawn.
- `ui/frame/hover-sweep`: the pointer moves from button to button: hit testing, hover restyle, repaint. `nodes` = 1012. `laid_out` = 0. `painted` = 1012. `layout_ms` = 3.54958e-05. `paint_ms` = 0.0955842. `runtime_ms` = 0.114567.
- `ui/layout/resize`: the window's width changes: the whole dashboard is laid out again (no raster). `nodes` = 1012. `laid_out` = 172. `painted` = 0. `layout_ms` = 0.0612961. `paint_ms` = 0. `runtime_ms` = 0.0762652.
- `ui/raster/full-1080p`: the whole dashboard's display list rasterised by the CPU renderer. `fill_rect_ms` = 0.903845. `fill_round_rect_ms` = 2.11543. `stroke_round_rect_ms` = 1.58359. `draw_glyphs_ms` = 0.636724. `push_transform_ms` = 0.01474. `pop_transform_ms` = 0.015437. `begin_node_ms` = 0.01354. `end_node_ms` = 0.012405.
- `ui/raster/full-1080p-parallel`: the same, in bands of 64 rows on the task pool (the same bytes).
- `ui/mount`: the dashboard is built, mounted and laid out (its first frame). `nodes` = 1012. `laid_out` = 10091. `painted` = 0. `layout_ms` = 4.23197. `paint_ms` = 0. `runtime_ms` = 4.37077.

### agent

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `agent/snapshot` | 714 | 0.559 ms | 0.577 ms | 0.592 ms | 0.625 ms | 8708.0 | p95_ms ≤ 5 ms | ✓ |

- `agent/snapshot`: the semantic snapshot of the dashboard, as the JSON an agent receives. `bytes` = 117393.

### text

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `text/paragraph` | 381 | 1.05 ms | 1.08 ms | 1.10 ms | 1.18 ms | 2930.0 | ns_per_item ≤ 1500 | ✓ |
| `text/glyph-motion` | 1967 | 0.205 ms | 0.224 ms | 0.228 ms | 0.256 ms | 43.0 | ns_per_item ≤ 400 | ✓ |

- `text/paragraph`: shaping and breaking 2 KB of text into lines 600 px wide. 455.6 ns an item. `height` = 457.365.
- `text/glyph-motion`: one frame of glyph motion over 2 KB of laid-out text: every glyph's style (D-68). 91.0 ns an item. `moving` = 0.882562.

### markdown

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `markdown/parse` | 888 | 0.453 ms | 0.468 ms | 0.485 ms | 0.570 ms | 4496.0 | ns_per_item ≤ 40 | ✓ |
| `markdown/stream-token` | 42 | 9.72 ms | 9.84 ms | 9.86 ms | 9.86 ms | 151357.0 | ns_per_item ≤ 5000 | ✓ |
| `markdown/to-html` | 3448 | 0.115 ms | 0.120 ms | 0.125 ms | 0.222 ms | 13.0 | ns_per_item ≤ 15 | ✓ |

- `markdown/parse`: parsing a 40 KB Markdown answer into the document (md4c and the block / inline model). 10.2 ns an item. `blocks` = 800.
- `markdown/stream-token`: appending a 40 KB answer 4 bytes at a time: the cost of one streamed token. 872.0 ns an item. `frozen_blocks` = 799.
- `markdown/to-html`: the document of a 40 KB answer back to HTML. 2.6 ns an item. `bytes` = 102052.

### render

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `render/effect-particles` | 114 | 3.52 ms | 3.57 ms | 3.60 ms | 3.61 ms | 4.0 | ns_per_item ≤ 60 | ✓ |

- `render/effect-particles`: a 480 × 320 layer bursting into particles: offscreen, effect, composite (D-68). 23.0 ns an item. `skipped` = 0.

### trace

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `trace/span-off` | 54748 | 0.007 ms | 0.007 ms | 0.008 ms | 0.053 ms | 0.0 | ns_per_item ≤ 5 | ✓ |
| `trace/span-on` | 1491 | 0.268 ms | 0.272 ms | 0.277 ms | 0.322 ms | 2.0 | ns_per_item ≤ 100 | ✓ |

- `trace/span-off`: 10 000 spans while nothing records: the cost tracing adds to a normal run. 0.7 ns an item.
- `trace/span-on`: 10 000 spans while a profile records. 26.8 ns an item.

### log

| benchmark | iterations | p50 | p95 | p99 | max | allocations | budget | |
|---|---:|---:|---:|---:|---:|---:|---|---|
| `log/filtered-out` | 49858 | 0.008 ms | 0.008 ms | 0.009 ms | 0.021 ms | 0.0 | ns_per_item ≤ 5 | ✓ |

- `log/filtered-out`: 10 000 debug records in a category the filter leaves out. 0.8 ns an item.

## Why each budget is there

- `ui/frame/one-label` p95_ms ≤ 16.67 ms: M1 acceptance ⑤: a 1000-node interface at 1080p on the CPU renderer holds 60 Hz
- `ui/frame/hover-sweep` p95_ms ≤ 16.67 ms: pointer motion over the same interface holds 60 Hz
- `ui/frame/all-labels` p95_ms ≤ 16.67 ms: even when every label of the interface changes in one frame
- `ui/frame/animate-all` p95_ms ≤ 16.67 ms: a strong-animation framework (D-60): every card of the interface in motion still holds 60 Hz
- `canvas/sharpen` p95_ms ≤ 300 ms: canvas-markdown §3.5: after the camera stops, what is on screen is sharp again within 300 ms
- `canvas/pan-1000` p95_ms ≤ 1 ms: canvas-markdown §3.5: panning a wall of 1000 windows costs under 1 ms of CPU a frame (raster aside)
- `canvas/zoom-sweep-1000` p95_ms ≤ 1 ms: canvas-markdown §3.5: zooming the same wall in and out costs under 1 ms of CPU a frame
- `canvas/pan-100k` p95_ms ≤ 1 ms: the grid index: a 100,000-node scene pans as cheaply as what is on screen
- `wall/zoom-sweep-1000` p95_ms ≤ 1 ms: S1 (canvas-markdown §3.5): a zoom sweep 1% ↔ 400% over 1000 pages costs under 1 ms of CPU a frame; nodes go live when the camera rests
- `wall/pan-1000` p95_ms ≤ 1 ms: S1: panning 1000 pages drawn as textures costs under 1 ms of CPU a frame
- `wall/workspace-drag-100` p95_ms ≤ 1 ms: S1: a workspace of 100 windows dragged by its label (a declarative behaviour) costs under 1 ms a frame
- `wall/selection-drag-1000` p95_ms ≤ 1 ms: S1: 1000 chosen windows dragged together, with snapping and the minimap, cost under 1 ms a frame
- `wall/stream-20` p95_ms ≤ 2 ms: S1 (§3.4): 20 windows written into at 50 tokens a second drop no frame at 120 Hz
- `wall/sharpen` p95_ms ≤ 300 ms: S1 (§3.5): after the camera stops over a wall of pages, every window on screen is sharp within 300 ms
- `canvas-kit/drag-100k` p95_ms ≤ 1 ms: canvas-markdown §1.1: dragging a selection with snapping, the grid and a minimap of 100,000 nodes costs under 1 ms of CPU a frame
- `glass/10-1080p-cpu` p95_ms ≤ 250 ms: the CPU reference of liquid glass (D-64) stays a usable preview: ten panels over a 1080p frame in bands on the task pool (about 110 ms on CI's four cores); real-time glass is the GPU's
- `glass/10-1080p-gpu` p95_ms ≤ 8.33 ms (unless software): D-64 and M4: ten glass panels over a 1080p frame at 120 Hz on a GPU (a software device, as CI's lavapipe, is reported, not held to it)
- `wall/zoom-sweep-1000-gpu` p99_ms ≤ 8.33 ms (unless software): S1 (§19.3): 1000 windows swept from 1% to 400% at 120 Hz, frame p99 under 8.3 ms, the GPU drawing every frame
- `styles/glass-gpu` p95_ms ≤ 8.33 ms (unless software): S2 (§19.3): the glass pack's interface at 120 Hz on a GPU, every frame drawn whole
- `styles/neon-gpu` p95_ms ≤ 8.33 ms (unless software): S2 (§19.3): the neon pack's interface at 120 Hz on a GPU, every frame drawn whole
- `styles/retro-gpu` p95_ms ≤ 8.33 ms (unless software): S2 (§19.3): the retro pack's interface at 120 Hz on a GPU, every frame drawn whole
- `styles/ink-gpu` p95_ms ≤ 8.33 ms (unless software): S2 (§19.3): the ink pack's interface at 120 Hz on a GPU, every frame drawn whole
- `styles/liquid-gpu` p95_ms ≤ 8.33 ms (unless software): S2 (§19.3): the liquid pack's interface at 120 Hz on a GPU, every frame drawn whole
- `ui/frame/idle` p95_ms ≤ 0.5 ms: a frame with nothing to do costs next to nothing
- `ui/frame/idle` max_allocations ≤ 0: no allocation per frame (AGENTS.md): the steady state allocates nothing
- `ui/mount` p95_ms ≤ 16.67 ms: building and laying out the whole interface fits in one frame
- `agent/snapshot` p95_ms ≤ 5 ms: an agent reads the whole tree of a 1000-node interface without stalling the UI thread
- `text/paragraph` ns_per_item ≤ 1500: shaping and breaking stay well under a microsecond and a half per byte
- `markdown/parse` ns_per_item ≤ 40: a Markdown answer parses at 25 MB/s or more: a 40 KB document in under 2 ms (about 10 ns per byte measured)
- `markdown/stream-token` ns_per_item ≤ 5000: a streamed token re-parses only the tail: under 5 µs each, so 20 streaming windows cost well under a millisecond a frame
- `markdown/to-html` ns_per_item ≤ 15: copy as HTML of a long answer stays below a millisecond
- `text/glyph-motion` ns_per_item ≤ 400: styling a glyph for a frame of motion stays far below its rasterisation: 2 000 animated glyphs cost under a millisecond
- `render/effect-particles` ns_per_item ≤ 60: the CPU reference of a particle burst stays interactive: a 480 × 320 layer under 10 ms (the GPU path is the fast one)
- `trace/span-off` ns_per_item ≤ 5: tracing that is off is a branch on a relaxed load (D-59): a few cycles even on a shared CI runner, so a frame's spans cost under a microsecond
- `trace/span-on` ns_per_item ≤ 100: recording a span takes no lock and no allocation (D-59)
- `log/filtered-out` ns_per_item ≤ 5: a log record the filter leaves out is not formatted (D-59): a branch on a relaxed load, a few cycles even on a shared CI runner
