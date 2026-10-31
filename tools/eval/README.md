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

An evaluation is one source among others. What was taken from it and what was not is written at the end of its report ("维护方的处理"), and in the round's summary (`reports/<date>-round-<n>.md`).
