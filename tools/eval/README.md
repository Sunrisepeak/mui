# tools/eval

`eval.sh` runs one evaluation by [the evaluation standard](../../.agents/docs/evaluation/README.md). codex, read-only, scores a subject (`.agents/docs/evaluation/subjects/*.md`) against the standard and the subject's top 3, with the subject's last record beside it. The report goes to `.agents/docs/evaluation/reports/<date>-<subject>.md`, and its scores are appended to `ledger.eval.omui.jsonl`.

The evaluator uses what it scores:

- The programs the subject's `**Run:**` list names are built with `--profile release` and started headless with `--serve`.
- codex reaches them through openmui's MCP server (`omui mcp`). It acts as a person with `ui_input` (pointer, wheel, keys, text, through the real input path) and as an agent with `ui_act` and `ui_invoke`. Each of its acts is decided by openas.
- It does the tasks in the subject's `**Use:**` list.
- The report begins with its use log and its feedback on the framework and openas.

```text
tools/eval/eval.sh site target/eval/site/*.png
tools/eval/eval.sh ui-framework
```

`duel.sh` has two codex agents play the fighting game against each other, then has a third analyse the match:

- The match is played by turns: `omui-fight --fight P1 P2 --turns N`. The stage's `turn` action says what a player holds for the next N frames.
- Each player is its own actor (`agent:codex-p1`, `agent:codex-p2`).
- The script records every turn: the stage's state as the semantic tree says it, and its picture.
- The analyst gets contact sheets of the recording, the log and the players' notes. It writes `reports/<date>-fighting-duel-<p1>-<p2>.md`.

```text
tools/eval/duel.sh kata mo
DUEL_TURN=6 DUEL_TURNS=300 tools/eval/duel.sh kun ni
```

`animate.py` is the animation task without a GUI (convergence plan §10.3): a real LLM agent — Claude Code (`claude -p`, the default) or codex — gets only hanim's skill, the document reference and the `hanim` command, and writes an animation document for each brief in [`animation-briefs.omui.json`](animation-briefs.omui.json) in an empty directory of its own. The grader checks each document through `hanim check` and `hanim sample` (the numbers at 600 moments, never the agent's word): where things start and end, bounces counted, overshoot, order and timing. Every trial and each run's success rate go to [`animation.eval.omui.jsonl`](../../.agents/docs/evaluation/animation.eval.omui.jsonl); an agent that cannot run (a quota, a login) is recorded as unavailable — neither a failure of the work nor a pass. `--grade DIR` grades one kept trial; each brief has a reference document that passes every check.

```text
tools/eval/animate.py --trials 3            # every brief, three times each
tools/eval/animate.py --agent codex --only bounce,spring
```

An evaluation is one source among others. What was taken from it and what was not is written at the end of its report ("维护方的处理"), and in the round's summary (`reports/<date>-round-<n>.md`).

`lead.py` keeps the lead ledger (Part 2 plan §2, D-135): `sample` tags a program's JSON lines with a series and a round; `verdict` pairs our series with each competitor's round by round, takes the ratio a round, its median and a seeded 95 % interval, and judges from the interval's lower end (behind < 0.95×, par, ahead ≥ 1.2×, far ahead ≥ 10×); `capability` records a capability against competitors (none, partial, app-defined, full); `summary` writes [`lead.md`](../../.agents/docs/evaluation/lead.md); `check` is lint `lead` (the signature features against the ledger). `tests/bench/competitors/run.sh --ledger` drives it.

```text
tests/bench/competitors/run.sh --ledger 15
tools/eval/lead.py capability --project omui --feature C --about "…" --test tools/mcp/tests/mcp_test.cpp --competitor webmcp=app-defined
tools/eval/lead.py summary --out .agents/docs/evaluation/lead.md
```

`eval.sh`'s codex scores are history since D-135: reviews compare side by side with similar projects (rubric `compare-v1`).

