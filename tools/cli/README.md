# omui — the openmui command line

```text
omui agent ls                                   the applications that listen
omui agent describe <app>                       interface, spec, operations
omui agent snapshot <app> [--root ID] [--depth N] [--interactive] [--offset N] [--limit N]
omui agent changes <app>
omui agent find <app> ['{"role":"button"}'] [--role R] [--name GLOB] [--state S] [--limit N]
omui agent act <app> <node> <action> [--args JSON] [--answer TEXT] [--actor ACTOR] [--dry-run]
omui agent undo|redo <app> [--scope ID] [--actor ACTOR] [--dry-run]      the history (S-12)
omui agent history <app> [--scope ID]           what can be undone and redone
omui agent transaction <app> '<steps JSON>' [--label TEXT] [--actor ACTOR] [--dry-run]   all or nothing (S-13)
omui agent wait <app> [--q JSON] [--absent] [--settled] [--timeout SECONDS]
omui agent capture <app> [--node ID] [--out FILE.png]
omui agent call <app> '<request JSON>'

omui agent inspect <app> [NODE]                 type, source line, props, layout, state
omui agent stats <app>                          frame times: mean, p50, p95, p99, max; phases; counts
omui agent profile <app> start|stop [--out FILE.trace.json]   per-span summary; Chrome trace for Perfetto
omui agent logs <app> [--level L] [--category C] [--limit N]
omui agent debug <app> [--bounds] [--repaints] [--inspector] [--hud] [--off] [--slow MS] [--log FILTER]
omui agent clock <app> [show|pause|resume|rate R|step S|seek T|release]   the application's time
omui agent pipeline <app> [show|replace|insert|remove] [--stage S] [--plugin P] [--after S] [--config JSON] [--toml]
```

An application started with `omui::run` takes the same switches at launch:
`--debug bounds,repaints,inspector,hud`, `--log layout=debug,*=info` (or the
`OMUI_LOG` environment variable) and `--profile FILE` (a trace of the whole
run, with its summary on stderr at exit).

Output is the envelope's records, one JSON object per line; the exit code is
the envelope's: 0 done, 1 failed, 2 needs an answer (an irreversible or
external action: call again with `--answer`), 3 not ready (a wait timed out).
Actions are recorded with their actor (`agent:omui-cli` unless `--actor`).

## omui video

Videos from descriptions (`*.video.omui.toml`) and view documents
(`*.view.omui.json`, rendered on the headless host's clock, a step a frame),
through [openmui.video](../../modules/media/video): one JSON object out and an
exit code (0 done, 1 failed, 2 a wrong command line).

```text
omui video render FILE --out OUT [--format y4m|png|wav] [--size WxH] [--fps R] [--seconds S | --frames N]
                       OUT: x.y4m (and x.wav beside it when there is sound), x.wav, shots/{:05}.png or shots/
omui video still  FILE (--frame N | --at SECONDS) --out x.png
omui video check  FILE                          its problems, or its size, rate, frames and samples a frame
omui video probe  FILE.y4m | FILE.wav           what it holds
omui video timing --fps R [--rate HZ] [--frames N]   each frame's first sample, length and start
```

`render` answers each file's name, size and hash and how many times faster
than real time it ran.

`add` and `bench` join as their members land.
