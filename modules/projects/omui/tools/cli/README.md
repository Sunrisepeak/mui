# omui — the openmui command line

`omui` is the one top-level command (D-150): a project from its start to its package, the running application operated by an agent, documents checked. The project commands go over mcpp (D-153: omui's build steps are mcpp plugins, so `omui build` is `mcpp build`); someone who is not a programmer needs only `omui`, and someone who is may use either.

```text
omui new NAME [--template app-ts]   a project (app-ts, app-cpp, component, native, theme; omui templates lists them)
omui dev [--headless] [-- ARGS]     the application in development mode: its own designer, a person and an agent designing it live (D-149)
omui run [--release] [-- ARGS]      the application as it ships (mcpp run)
omui build [--release]              mcpp build
omui build --web [--lang L]         the page on the Web: the program run natively writes its first screen as static HTML
                                    (target/web/<program>.static.html, D-236), then mcpp pack --target wasm32-emscripten
                                    --release --pkg-format dist-toy puts it in the page (build.mcpp's dist_toy says how)
omui test [-- CASES]                its tests, mtest's (mcpp test)
omui doctor                         which tools are there, which are missing, what each is for
```

Every project command is an operation of its catalog ([`cli.ops.xdef.ts`](cli.ops.xdef.ts)): the subject `omui` behind the tool kernel's entries (mlibs.agent:host). `omui build --release` is the operation omui.build; words without names are its parameters in the catalog's order (`omui new hello`); the answer is records a line each — what it ran (`{"step", "command", "exit"}`) and what it read as data, an error with its ways out, and last the result with the exit code (0 done, 1 failed, 2 arguments that make no request). `omui mcp` serves the same operations as `omui_*` tools — except dev, run, connect and mcp, which hold a window or the terminal (`terminal` in the catalog): a person's command line runs them, and an agent's entry is refused with the command to type. No command has a table of its own: `omui` alone and `omui --help` list them, `omui <command> --help` describes one whole.

A timeline's gain automation auditioned (a WAV, an omui view's frames, a report) is a composition of hanim, meng.audio and omui, so it is xtools': [`xtools audition.automation`](../../../xtools/audition).

`omui agent <operation> <app> …` and `omui page <operation> …` are the protocol's catalogs (omui.agent: `ui.*`, and the verbs `design.*`, `generate.*`, `task.*`) through the same tool kernel: an operation's words are its parameters in the catalog's order, or `--name value` (`--dry-run` is `dry_run`; a switch alone is true, or takes the `true`/`false` after it); `--actor` says who acts (default `agent:omui-cli` for what changes something); `--out FILE` keeps a picture, its vectors or a trace the answer carries. `omui agent <operation> --help` describes one whole. A `ui.*` operation goes out as the protocol's request, a verb as `invoke`; the application decides what it admits.

```text
omui agent ls                                   the applications that listen
omui agent describe <app>                       interface, spec, operations
omui agent snapshot <app> [--root ID] [--depth N] [--interactive] [--offset N] [--limit N]
omui agent changes <app>
omui agent find <app> '{"role":"button","name":"Save*"}'
omui agent act <app> <node> <action> [--args JSON] [--answer TEXT] [--actor ACTOR] [--dry-run]
omui agent undo|redo <app> [--scope ID] [--dry-run]      the history (S-12)
omui agent history <app> [--scope ID]           what can be undone and redone
omui agent transaction <app> '<steps JSON>' [--label TEXT] [--dry-run]   all or nothing (S-13)
omui agent wait <app> [--q JSON] [--absent JSON] [--settled] [--timeout SECONDS]
omui agent capture <app> [--node ID] [--format svg] [--out FILE]
omui agent timeline | explain | drew <app> …    what input did; one press, whole; which node drew a pixel
omui agent design.begin <app> "<intent>" …      Live Design's verbs, as every verb: its parameters in order
omui agent call <app> '<request JSON>'          one request as the protocol has it
omui agent sweep <app> [--root ID] [--actor ACTOR]   every action of every node an agent can operate, performed

omui agent inspect <app> [NODE]                 type, source line, props, layout, state
omui agent stats <app>                          frame times: mean, p50, p95, p99, max; phases; counts
omui agent profile <app> start|stop [--chrome --out FILE.trace.json]   per-span summary; Chrome trace for Perfetto
omui agent logs <app> [--level L] [--category C] [--limit N]
omui agent debug <app> [--bounds] [--repaints] [--inspector] [--hud] [--slow-frame-ms MS] [--log FILTER]   each overlay on, or `false` off
omui agent clock <app> [show|pause|resume|release]; clock <app> rate R; clock <app> step|seek --seconds S
omui agent pipeline <app> [show|replace|insert|remove] [--stage S] [--plugin P] [--after JSON] [--config JSON]
```

An application started with `omui::run` takes the same switches at launch:
`--debug bounds,repaints,inspector,hud`, `--log layout=debug,*=info` (or the
`OMUI_LOG` environment variable) and `--profile FILE` (a trace of the whole
run, with its summary on stderr at exit).

Output is the envelope's records, one JSON object per line; the exit code is
the envelope's: 0 done, 1 failed, 2 needs an answer (an irreversible or
external action: call again with `--answer`), 3 not ready (a wait timed out).
Actions are recorded with their actor (`agent:omui-cli` unless `--actor`).

`sweep` is the interaction sweep (D-91; every program in CI,
`omui-devtools check sweep`): leaves first, each ordinary action asked about (a dry run),
then performed — what can be undone, undone, and the node must be as it was;
what an answer is a person's (irreversible, external) only asked. A verdict a
line: `ok`, `asks`, `disabled`, `readonly`, `gone` (an action before took it
out of the tree), `arguments` (made-up arguments refused: it needs real ones),
`unrecorded` (no inverse given, which S-12 allows), `person` (a person-only
action actually refused to the agent with the node unchanged, S-10), `failed` (refused with
none to make up, or not as it was after its undo). A failure exits 1.

## Films

Films are the engine's: the [`video`](../../../meng/kernel/video/tool) command renders descriptions, takes stills,
checks, probes and shows the timing. A view's frames (a view document drawn on the headless host's clock, a step a
frame) and the kinds of its data modules are omui's sources, given to the toolkit where both are known —
[xtools](../../../xtools), in the engine's tier (D-231): `xtools video render hello.view.xdef.json --out shots/`.
This package does not build the engine in. `omui design film` makes a design's frames with the application and
runs `xtools video render` on them; without xtools it says so, and the frames stay where they were made.

`add` and `bench` join as their members land.
