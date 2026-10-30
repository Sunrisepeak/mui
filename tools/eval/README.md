# tools/eval

`eval.sh` runs one evaluation by [the evaluation standard](../../.agents/docs/evaluation/README.md): codex, read-only, scores a subject (`.agents/docs/evaluation/subjects/*.md`) against the standard and the subject's top 3, with its last record beside it. The report is written to `.agents/docs/evaluation/reports/<date>-<subject>.md`, and its scores are appended to `ledger.eval.omui.jsonl`.

```text
tools/eval/eval.sh site target/eval/site/*.png
tools/eval/eval.sh ui-framework
```

An evaluation is one source among others. What was taken from it and what was not is written at the end of its report ("维护方的处理").
