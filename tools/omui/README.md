# omui — the openmui command line

```text
omui agent ls                                   the applications that listen
omui agent describe <app>                       interface, spec, operations
omui agent snapshot <app> [--root ID] [--depth N] [--interactive] [--offset N] [--limit N]
omui agent changes <app>
omui agent find <app> ['{"role":"button"}'] [--role R] [--name GLOB] [--state S] [--limit N]
omui agent act <app> <node> <action> [--args JSON] [--answer TEXT] [--actor ACTOR]
omui agent wait <app> [--q JSON] [--absent] [--settled] [--timeout SECONDS]
omui agent capture <app> [--node ID] [--out FILE.png]
omui agent call <app> '<request JSON>'

omui agent inspect <app> [NODE]                 type, source line, props, layout, state
omui agent stats <app>                          frame times: mean, p50, p95, p99, max; phases; counts
omui agent profile <app> start|stop [--out FILE.trace.json]   per-span summary; Chrome trace for Perfetto
omui agent logs <app> [--level L] [--category C] [--limit N]
omui agent debug <app> [--bounds] [--repaints] [--inspector] [--hud] [--off] [--slow MS] [--log FILTER]
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

`check`, `fmt`, `add`, `video` and `bench` join as their members land.
