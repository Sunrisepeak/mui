# hanim.timeline.tool

The `hanim-timeline` command: the timeline every editor shares ([hanim.timeline](..)) without a GUI, for people, agents and CI.

```sh
omui-gen snapshot showcase/promo-video/short.timeline.omui.ts --out short.timeline.omui.json   # the JSON it works on
hanim-timeline check short.timeline.omui.json      # tracks, clips, markers, length — or every problem at its pointer, line:column
hanim-timeline at short.timeline.omui.json --time 1s                       # what plays: each clip, where in its source, at what level
hanim-timeline edit short.timeline.omui.json split --track 0 --clip 0 --at 15f
hanim-timeline undo short.timeline.omui.json                               # the file's text before the edit, exactly
```

One JSON answer on standard output; exit 0 done, 1 refused or failed (why), 2 a wrong command. An edit refused (outside its clip, an overlap) leaves the file as it was; the text before each edit is kept beside the file (`.<file>.history.omui.jsonl`, the last 64).

Its operations are a catalog ([`timeline.ops.omui.ts`](timeline.ops.omui.ts), listed in [`media/ops`](media/ops)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Checked end to end by `tools/ci/tools-e2e.sh`.
