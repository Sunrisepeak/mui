# hanim.timeline.tool

The `hanim-timeline` command: the timeline every editor shares ([hanim.timeline](..)) without a GUI, for people, agents and CI.

```sh
xdef snapshot showcase/promo-video/short.timeline.omui.ts --write   # the JSON it works on
hanim-timeline timeline.check --file short.timeline.omui.json # tracks, clips, markers, length — or every problem at its pointer, line:column
hanim-timeline timeline.at --file short.timeline.omui.json --time 1s # what plays: each clip, where in its source, at what level
hanim-timeline timeline.edit --file short.timeline.omui.json --edit split --track 0 --clip 0 --at 15f
hanim-timeline timeline.undo --file short.timeline.omui.json # the file's text before the edit, exactly
```

Records on standard output, a line each, the data first and the result last; exit 0 done, 1 refused or failed (why, and the ways out), 2 arguments that make no request. An edit refused (outside its clip, an overlap) leaves the file as it was; the text before each edit is kept beside the file (`.<file>.history.omui.jsonl`, the last 64).

Its operations are a catalog ([`timeline.ops.omui.ts`](timeline.ops.omui.ts), listed in [`src/catalog`](src/catalog); the command is the subject `timeline` behind the tool kernel's entries, and `hanim-timeline mcp` offers the same operations to an MCP client); how an Agent uses them is [`skill.omui.md`](skill.omui.md). Checked end to end by `omui-devtools check tools-e2e`.
