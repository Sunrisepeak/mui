# openmui.agent

The agent endpoint of an openmui application (spec §9, plan §11):
`openmui.tree@0.1` as JSON lines.

- `Protocol` — one request per line (`describe`, `snapshot`, `changes`,
  `find`, `act`, `wait`, `capture`), answered by envelope records ending in a
  `result`; `"re"` correlates records with requests; `wait` answers when the
  condition holds (checked after every frame) or times out with exit code 3;
  `act` applies the effect rules (`needs-answer`, exit code 2) and records the
  actor (`agent:planner;via=person:alice`) in the runtime's journal. Errors
  carry hints that show the correct form.
- Development operations (D-59): `inspect` (a node's type, source line,
  props, layout and state), `stats` (frame-time mean and percentiles, the
  phases, node counts), `profile` (`start`, then `stop` returns the per-span
  summary and, with `"chrome":true`, the Chrome trace), `logs` (recent
  records by level and category) and `debug` (the runtime's overlays, the
  slow-frame threshold and the log filter, while it runs), and `pipeline`
  (the frame's stages, plugins and times; `replace`, `insert` and `remove`
  a stage between frames — D-66), and `clock` (pause, resume, rate, step,
  seek, release: the application's time; seeking back replays animations).
- `Endpoint` — listens through `mcpplibs.os.ipc` (a local socket) in a
  per-user directory, writes `<name>.endpoint.omui.json` beside it, reads
  connections on background threads and serves requests on the UI thread in
  `pump()`, between frames. Each connection has its own `changes` baseline.
- `Client` and `list()` — what `omui agent` uses.

```text
→ {"op":"find","q":{"role":"button","name":"Delete*"}}
← {"kind":"data","data":{"nodes":[{"id":"notes/delete","role":"button","actions":[{"name":"activate","effect":"irreversible","answer":"yes"}]}]}}
→ {"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"}
← {"kind":"error","code":"needs-answer","message":"…","answer":["yes"],"hint":"call again with \"answer\": \"yes\""}
← {"kind":"result","exitCode":2}
```

Depends on `openmui.core`, `openmui.view`, `openmui.semantic`, `openmui.trace` and `mcpplibs.os`.
