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
```

Output is the envelope's records, one JSON object per line; the exit code is
the envelope's: 0 done, 1 failed, 2 needs an answer (an irreversible or
external action: call again with `--answer`), 3 not ready (a wait timed out).
Actions are recorded with their actor (`agent:omui-cli` unless `--actor`).

`check`, `fmt`, `add`, `video` and `bench` join as their members land.
