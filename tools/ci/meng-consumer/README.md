# An independent consumer of meng

This fixture is materialized in a fresh temporary directory outside the repository workspace by `omui-devtools consumer meng` (the closure checklist's item 9 for meng). Its only dependencies are meng's public packages and the foundation libraries they need, by path: `meng.runtime` and `meng.input` (the game layer), `meng.cellular` (the engine's kernel), `mlibs.agent`, `mlibs.json` and `mlibs.utils`. No dev dependencies, no omui.

The program is a small game: a 24 × 24 field of cells under one `meng.cellular` rule (a cell catches from a burning neighbour, every fourth step), the state of a `meng.runtime` game whose player walks the middle row and sets a spark down. It is played through the agent kernel as an agent plays it: `world.act`, `world.step`, `world.observe`, `world.branch` and `world.seek`. One JSON line says what held:

- the same requests on a second game give the same hash;
- a branch 20 steps ahead burns more and leaves the game's hash as it was;
- sought back to the spark's frame, the game's hash is the one it had there.

```sh
omui-devtools consumer meng
omui-devtools consumer meng --keep-project --output /tmp/meng-consumer.report.xdef.json
```

The probe builds and runs the program in the `dev` and `release` profiles (its own lock first, then `--locked`), requires the same hash from both, and audits what mcpp actually resolved and compiled: no package of omui, hanim, mrender, gongfu or the openmui specification in the graph, and no repository source compiled outside `modules/projects/meng/{kernel,game}` and `modules/base`. The repository's lock must not change.

This is meng consumed from this checkout's paths. Item 9 asks for the published packages; the fixture takes them once the release channel (RL2) publishes meng. No window, sound, GPU or performance is claimed.
