# xengine.tool

The `xengine` command: xengine's shared editing pieces without a GUI, for people, agents and CI. Now the timeline every editor shares ([xengine.timeline](../timeline)).

```sh
omui-gen snapshot showcase/promo-video/short.timeline.omui.ts --out short.timeline.omui.json   # the JSON it works on
xengine timeline check short.timeline.omui.json      # tracks, clips, markers, length — or every problem at its pointer, line:column
xengine timeline at short.timeline.omui.json --time 1s                       # what plays: each clip, where in its source, at what level
xengine timeline edit short.timeline.omui.json split --track 0 --clip 0 --at 15f
xengine timeline undo short.timeline.omui.json                               # the file's text before the edit, exactly
```

One JSON answer on standard output; exit 0 done, 1 refused or failed (why), 2 a wrong command. An edit refused (outside its clip, an overlap) leaves the file as it was; the text before each edit is kept beside the file (`.<file>.history.omui.jsonl`, the last 64).

Its operations are a catalog ([`xengine.ops.omui.ts`](xengine.ops.omui.ts), listed in [`media/ops`](media/ops)); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Checked end to end by `tools/ci/tools-e2e.sh`.
