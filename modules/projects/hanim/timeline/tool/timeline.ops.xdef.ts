// The timeline's operations (convergence plan §10.3): what an Agent does with
// the timeline every editor shares (hanim.timeline; its JSON,
// *.timeline.xdef.json) through the `hanim-timeline` command — the subject
// `timeline` behind the tool kernel's entries (`hanim-timeline timeline.check
// --file F`, `hanim-timeline mcp`; records out, exit 0 done, 1 refused or failed
// with why). ops-gen makes the catalog (src/catalog/catalog.cppm), the
// listing (timeline.ops.json) and the reference (timeline.ops.md); effects are
// openas's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "timeline",
  module: "hanim.timeline.tool",
  namespace: "hanim::timeline::tool",
  doc: "Check timelines, read what plays at a moment, make the usual edits and take them back.",
  generated: "src/catalog",
  operations: [
    {
      name: "timeline.check",
      effect: "read",
      summary: "The timeline (rate, size, tracks and clips, markers, length), or every problem at its line and column",
      params: [{ name: "file", type: "path", doc: "a *.timeline.xdef.json (what a .timeline.xdef.ts evaluates to: xdef snapshot)" }],
      result: "json",
    },
    {
      name: "timeline.at",
      effect: "read",
      summary: "What plays at a moment: each clip, where in its source, at what level",
      params: [{ name: "file", type: "path" }, { name: "time", type: "string", doc: "90f, 1.5s, 250ms or 123fl" }],
      result: "json",
    },
    {
      name: "timeline.edit",
      effect: "reversible",
      summary: "Split, trim, move, remove or ripple-remove a clip; the file written as the edit leaves it (or only said, with dry_run)",
      params: [
        { name: "file", type: "path" },
        { name: "edit", type: "string", doc: "split, trim, move, remove or ripple-remove" },
        { name: "track", type: "integer", doc: "the track's index (0: the first)" },
        { name: "clip", type: "integer", doc: "the clip's index on its track" },
        { name: "at", type: "string", doc: "split: where; trim: the new start; move: the new start", optional: true },
        { name: "length", type: "string", doc: "trim: the new length", optional: true },
        { name: "to_track", type: "integer", doc: "move: another track of its kind", optional: true },
        { name: "dry_run", type: "bool", doc: "only say what it would leave", optional: true },
      ],
      result: "json",
      undo: "timeline.undo",
    },
    {
      name: "timeline.undo",
      effect: "reversible",
      summary: "Take back the last edit this command made to the file (its text before it restored exactly; the last 64 kept)",
      params: [{ name: "file", type: "path" }],
      result: "json",
      undo: "make the edit again (timeline.edit)",
    },
  ],
});
