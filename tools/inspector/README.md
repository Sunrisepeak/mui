# openmui Inspector

A tool written in openmui that inspects any running openmui application
(D-61, D-63, D-67): the first flagship of F1 (unified semantics) and F4
(time). It connects to the application's agent endpoint — the same
`openmui.tree@0.1` protocol `omui agent` speaks — and shows:

- **the semantic tree**, a virtualised list (`list_view` over a model), indented by depth;
- **the selected node**: type, contract, source line and function, bounds,
  properties, state, layout style, bindings, handlers, transitions (`inspect`);
- **frames**: the last 120 frame times as a graph against the 60 Hz budget,
  the mean, p95 and max, and the mean time of each phase (`stats`);
- **the pipeline**: each stage, its plugin and its time (`pipeline`);
- **the clock**: pause and resume, a frame back or forward, slow motion,
  back to live time (`clock`) — animations go back in time with it;
- **overlays** on the application: bounds, repaints, the frame-time HUD (`debug`).

```text
mcpp run -p openmui.inspector -- [--app org.example.notes] [--inspect-dir DIR]
```

Without `--app` it inspects the first application that listens (never
itself). Requests run on the task pool, one round at a time — a round starts
only when the last one was answered — and come back through the UI queue, so
the Inspector's frames never wait for the other process.

Everything it shows, `omui agent inspect | stats | pipeline | clock | debug`
prints as JSON: one set of data, for people and for agents.

The test drives a real application over a real endpoint: the tree arrives,
a node's details, frames and the pipeline; the Inspector's buttons pause the
application's clock, step it by exactly one frame, turn an overlay on, and
return it to live time.
