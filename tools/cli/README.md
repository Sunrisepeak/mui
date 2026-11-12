# omui — the openmui command line

`omui` is the one top-level command (D-150): a project from its start to its package, the running application operated by an agent, documents checked. The project commands go over mcpp (D-153: omui's build steps are mcpp plugins, so `omui build` is `mcpp build`); someone who is not a programmer needs only `omui`, and someone who is may use either.

```text
omui new NAME [--template app-ts]   a project (app-ts, app-cpp, component, native, theme; --list)
omui dev [--headless] [-- ARGS]     the application in development mode: its own designer, a person and an agent designing it live (D-149)
omui run [--release] [-- ARGS]      the application as it ships (mcpp run)
omui build [--release]              mcpp build
omui test [-- CASES]                its tests, mtest's (mcpp test)
omui doctor                         which tools are there, which are missing, what each is for
```

Each says what it ran; `--json` gives one record a step (`{"step", "command", "exit"}`) and the exit code is the tool's, for an agent.

`omui audio automation FILE --track NAME --clip NAME --out PREFIX [--property gain] [--seconds 2] [--fps 60]` auditions a named timeline gain property on a 220 Hz calibration tone. It exports a float WAV, a labeled eight-frame PNG sheet drawn by omui, a lossless WebP process and a timestamped `.report.omui.json`; no window or audio device is opened. `--fps` changes visual observation only. Duration is 1–10 whole seconds; the visual rate is an exact timeline rate in 1–120 fps (for example 30, 60 or 120). See the [public timeline/audio adapter and minimal SOP](../../modules/projects/xengine/audio/timeline/README.md) for the sample-clock, ownership, safe binding and failure boundaries. Exit codes are 0 success, 1 domain/output failure, 2 invalid command arguments. A later output failure lists files already written.

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
omui agent sweep <app> [--root ID] [--actor ACTOR]   every action of every node an agent can operate, performed

omui agent inspect <app> [NODE]                 type, source line, props, layout, state
omui agent stats <app>                          frame times: mean, p50, p95, p99, max; phases; counts
omui agent profile <app> start|stop [--out FILE.trace.json]   per-span summary; Chrome trace for Perfetto
omui agent logs <app> [--level L] [--category C] [--limit N]
omui agent debug <app> [--bounds] [--repaints] [--inspector] [--hud] [--off] [--slow MS] [--log FILTER]
omui agent clock <app> [show|pause|resume|rate R|step S|seek T|release]   the application's time
omui agent pipeline <app> [show|replace|insert|remove] [--stage S] [--plugin P] [--after S] [--config JSON] [--description]
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
`tools/ci/sweep.sh`): leaves first, each ordinary action asked about (a dry run),
then performed — what can be undone, undone, and the node must be as it was;
what an answer is a person's (irreversible, external) only asked. A verdict a
line: `ok`, `asks`, `disabled`, `readonly`, `gone` (an action before took it
out of the tree), `arguments` (made-up arguments refused: it needs real ones),
`unrecorded` (no inverse given, which S-12 allows), `person` (a person-only
action actually refused to the agent with the node unchanged, S-10), `failed` (refused with
none to make up, or not as it was after its undo). A failure exits 1.

## omui video

Videos from descriptions (`*.video.omui.ts`, evaluated, or their JSON) and view documents
(`*.view.omui.json`, rendered on the headless host's clock, a step a frame),
through [xengine.video](../../modules/projects/xengine/video) and its WebM plugin: one
JSON object out and an exit code (0 done, 1 failed, 2 a wrong command line).

```text
omui video render FILE --out OUT [--format webm|y4m|png|wav] [--size WxH] [--fps R] [--seconds S | --frames N]
                       [--quality Q | --bitrate KBPS] [--speed N] [--realtime] [--lossless] [--threads N]
                       OUT: x.webm (VP9 and Opus), x.y4m (and x.wav beside it when there is sound), x.wav,
                       shots/{:05}.png or shots/
omui video still  FILE (--frame N | --at SECONDS) --out x.png
omui video check  FILE                          its problems, or its size, rate, frames and samples a frame
omui video probe  FILE.webm | FILE.y4m | FILE.wav   what it holds (a WebM decoded: frames and samples counted)
omui video timing --fps R [--rate HZ] [--frames N]   each frame's first sample, length and start
```

`render` answers each file's name, size and hash and how many times faster
than real time it ran.

`add` and `bench` join as their members land.
