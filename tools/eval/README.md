# tools/eval

The evaluations' data: the agent task sets (`tasks/`), the cold-start sets (`coldstart/`), the animation briefs ([`animation.briefs.xdef.json`](animation.briefs.xdef.json)) and the comparison sets' material (made by `omui-devtools eval materials`). The evaluations themselves are `omui-devtools eval` ([tools/devtools](../devtools/README.md)), C++ under mcpp (D-206). Each uses what it judges: a real agent — a fresh `claude -p` session, or codex — in an empty directory of its own, the outcome checked on the subject's own state or numbers, never on the agent's word. An agent that cannot run (a quota, a login) is recorded as unavailable: neither a failure nor a pass.

An evaluation is one source among others. What was taken from it and what was not is written at the end of its report ("维护方的处理"), and in the round's summary (`reports/<date>-round-<n>.md`). The lead ledger (Part 2 plan §2, D-135) is kept by `omui-devtools lead` and measured by `omui-devtools bench competitors --ledger` ([tools/devtools](../devtools/README.md#lead)).

## compare

The side-by-side reviews (D-135, rubric `compare-v1`). A comparison set (`.agents/docs/evaluation/compare/<subject>.compare.xdef.json`) pairs our picture with a similar project's of the same scene, per dimension; `omui-devtools eval materials controls` makes them (our controls from the site's quality lab beside Material 3's from Google's @material/web, the same four states and cells). `sheets` blinds each pair (A and B; which is ours drawn from a stream seeded by the subject and the set's seed; the key kept apart under `target/compare/<subject>/`); `judge` asks fresh Claude sessions that have nothing of the project — an empty directory, the sheet, the question — three a sheet, the majority deciding (a tie is par), unblinds each answer into ahead, par or behind, and gives a dimension its weakest pair's verdict; `record` writes the maintainer's verdict, which decides.

```text
omui-devtools eval materials controls
omui-devtools eval compare sheets components && omui-devtools eval compare judge components
omui-devtools eval compare record components controls par --note "…"
```

The codex scores of the evaluation standard's first rounds (`eval.sh`) are history since D-135, kept in the ledger and the reports; reviews compare side by side.

## tasks

Agent task sets (Part 2 plan §6.3, D-135): a goal in words, an agent given only an application's interface, the outcome checked on the application's state. Each task runs on a fresh instance of the set's program (built here, `--profile release`), headless, its endpoint in an empty directory; the agent has one tool server, `omui mcp --way WAY`:

- `semantic` — the application's semantic tree and its operations (actions, verbs, preview, undo; what needs the person asks them);
- `a11y-tree` — what a browser agent's accessibility tree gives (Playwright MCP's snapshot): roles, names, values, states and bounds by ref, a picture, a person's devices — no operations of the application;
- `screenshot` — what computer use gives: a picture and a person's devices at coordinates.

Only the interface differs (D-142). The checks run through `omui mcp` as a service: `present` queries must match, `absent` must not, `state` values must be what a verb's data holds at a JSON pointer, `answer` must be in the agent's last message. A task marked `person` needs the person's decision: an agent that does it anyway has overreached — the measure that has to be 0. Each trial is a line of `tasks.eval.xdef.jsonl`: success, the tools it called, its turns, overreach, and its tokens (`tokens` the fresh ones — input, cache writes, output — and `usage` each part, cache reads apart).

```text
omui-devtools eval tasks run ui --via a11y-tree --trials 2
omui-devtools eval tasks summary ui --split-only
```

## cold-start

A fresh agent session that knows nothing of the project — no skill, no files to read — is given one program it may run (`./lab`, the program with its arguments) and one task in words (D-186, D-192). The program has to teach itself: `subjects`, `describe`, its tasks and examples, the ways out of every error. The outcome is checked afterwards through the same program, as requests on the state the agent left, never on its word; the last message only for what the task asks it to say. Each trial is a line of `coldstart.eval.xdef.jsonl`: success, the commands it ran, its turns, its tokens (fresh apart from cache reads), how long. `coldstart/matter-lab` makes the material lab's two trials of 2026-11-14 repeatable (RC2.18).

A site is met cold at two of its doors (0.1.0 plan AG6; oweb's design §4.10), over the minimal documents (oweb's template) built in the trial's directory. `coldstart/site`: the command's door — `./site` is the `oweb` command, the agent's words passed to it as they are (a set's `wrapper` with `words`), and the agent is told to begin with `./site subjects` and where the built site is. Its first twelve trials (2026-11-27) are failures of the set, not of the command: its wrapper then piped requests to a command that reads none from its standard input, and every trial stopped there — they stay in the ledger as they were. `coldstart/site-address`: the address — the agent can only fetch; `./fetch PATH` prints what a static host of the site sends for a path (a file's text, a directory's `index.html`, `404 not found`; a whole address from the site's stated one is read as its path), which is the set's `wrapper` with `host` in place of `args`. The same four questions at both, answered by the documents' own words — a command in a table, the generated reference, a code example, and one asked in Chinese of the Chinese pages — each checked on the last message. A trial's record also keeps the commands it ran (`commands`): which of a site's files a fetching agent found, and where a failed one went.

Measured 2026-11-27 (three trials a question): by the command 12 of 12 — 5 commands, 9.7k fresh tokens and 24 s at the median; by the address 12 of 12 — 3 commands, 10.2k fresh tokens, 15 s. The run before the command's last passed 12 of 12 too, with 7 commands at the median and up to 19: an agent that had read a parameter's name typed `site=/path`, the command line took the word as an unnamed value, and the answer was of a place that is not there. The kernel's command line reads `name=value` as that parameter since (and `describe operation X`, `describe --help`, a request typed whole) — the fix is the owner's, the questions are as they were.

```text
omui-devtools eval cold-start matter-lab --trials 3
omui-devtools eval cold-start matter-lab --only resin --keep
omui-devtools eval cold-start site --trials 3
omui-devtools eval cold-start site-address --trials 3
```

## animate

The animation task without a GUI (convergence plan §10.3): a real agent — Claude Code (the default) or codex — gets only hanim's skill, the document reference and the `hanim` command, and writes an animation document for each brief in an empty directory of its own. The grader checks each document through `hanim document.check` and `hanim document.sample` (the numbers at 601 moments): where things start and end, bounces counted, overshoot, order and timing. Every trial and each run's success rate go to [`animation.eval.xdef.jsonl`](../../.agents/docs/evaluation/animation.eval.xdef.jsonl). `--grade DIR` grades one kept trial; each brief has a reference document that passes every check. How opaque an object is shown is its fill's alpha through its own opacity (a recipe that fades an object in moves its opacity and leaves its fill). The 601 moments are asked in one request: as `--at` words they are 1,202 arguments, more than openkal-linux 0.26 starts a program with (512) — the eight trials of 2026-11-27 at `d57aa843` recorded as failed with `[]` are that, not the documents; the run after it passed 8 of 8.

```text
omui-devtools eval animate --trials 3
omui-devtools eval animate --agent codex --only bounce,spring
```

## duel

Two codex agents play the fighting game against each other by turns (`omui-fight --fight P1,P2 --turns N`: the stage's `turn` action says what a player holds for the next N frames), each its own actor (`agent:codex-p1`, `agent:codex-p2`). Every turn is recorded — the stage's state as the semantic tree says it, and its picture — and a third codex, read-only, gets contact sheets of the recording, the log and the players' notes, and writes `reports/<date>-fighting-duel-<p1>-<p2>.md`.

```text
omui-devtools eval duel kata mo
omui-devtools eval duel kun ni --turn 6 --turns 300
```

## hot-reload

Save to screen (W2: ≤ 100 ms at p95): a TypeScript application run in development mode with a real clock, its script saved again and again with a word of its title changed; how long until the semantic tree shows the new title, read through the agent protocol (so the figure includes that read).

```text
omui-devtools eval hot-reload --omui OMUI --program PROGRAM --app-dir examples/02-hello-ts
```
