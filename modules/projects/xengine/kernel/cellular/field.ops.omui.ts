// A cellular field's operations as a subject of the agent kernel (D-173,
// D-174): what an Agent does with a field of cells run by a prepared program —
// read a region as numbers, place values, step it, return to a frame, ask
// what would be. omui-gen ops makes the table (src/agent/catalog.cppm) and the
// listings (src/agent/field.ops.json, field.ops.md); effects are openas's,
// admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "field",
  module: "xengine.cellular.agent",
  namespace: "xengine::cellular::agent",
  generated: "src/agent",
  doc: "Read a field of cells as numbers, place values in it, step it, return to a frame, ask what would be, and who changed a cell.",
  operations: [
    {
      name: "field.observe",
      effect: "read",
      summary: "The frame, the steps run, the state's hash, the field's shape, and each channel of a region as numbers: least, most, sum, cells not zero",
      params: [
        { name: "region", type: "json", doc: "[x, y, width, height] in cells (the whole field when none is given)", space: "lattice-2d", optional: true },
        { name: "budget", type: "integer", doc: "the most bytes of answer: channels are left out from the last, and `more` says how many", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "field.cells",
      effect: "read",
      summary: "One channel's values in a region, row by row; at most 4096 cells a request",
      params: [
        { name: "channel", type: "integer", doc: "which channel" },
        { name: "region", type: "json", doc: "[x, y, width, height] in cells", space: "lattice-2d" },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "field.place",
      effect: "reversible",
      summary: "Sets one channel to a value over a region; a frame of its own, recorded like a step",
      params: [
        { name: "channel", type: "integer", doc: "which channel" },
        { name: "value", type: "integer", doc: "0 … 4294967295" },
        { name: "region", type: "json", doc: "[x, y, width, height] in cells", space: "lattice-2d" },
      ],
      result: "json",
      cost: "bounded",
      undo: "field.seek to the frame before it",
    },
    {
      name: "field.step",
      effect: "reversible",
      summary: "Runs the program a number of steps over the field: the tiles awake (what is at rest sleeps, the result the same); answers where the field is and how many of its tiles sleep",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "field.seek to the frame it began at",
    },
    {
      name: "field.seek",
      examples: [{ frame: 1 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept; what is run from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the field: what came after the frame is another branch's",
    },
    {
      name: "field.branch",
      effect: "read",
      summary: "What would be: the field some steps ahead, a value placed first when one is given, observed as field.observe does; the field itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "channel", type: "integer", doc: "with `value` and `place`: placed before the steps", optional: true },
        { name: "value", type: "integer", optional: true },
        { name: "place", type: "json", doc: "[x, y, width, height] in cells", space: "lattice-2d", optional: true },
        { name: "region", type: "json", doc: "the region observed (the whole field when none is given)", space: "lattice-2d", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "field.explain",
      effect: "read",
      summary: "Why a cell holds what it holds, as far as the field knows: its values, the edits on record that cover it — what was written, at which frame, by whom — the latest first, and what the program changed of it in the last step",
      params: [{ name: "cell", type: "json", doc: "[x, y]", space: "lattice-2d" }],
      result: "json",
      cost: "bounded",
    },
  ],
  tasks: [
    {
      name: "read-the-field",
      summary: "Read the field's channels in brief, then one channel's cells in a region",
      steps: [
        { op: "field.observe" },
        { op: "field.cells", channel: 0, region: [0, 0, 4, 4] },
      ],
    },
    {
      name: "place-step-and-look",
      summary: "Place a value, step the rules, and read what became of it",
      steps: [
        { op: "field.place", channel: 0, value: 1, region: [9, 0, 1, 3] },
        { op: "field.step", steps: 5 },
        { op: "field.observe" },
      ],
    },
    {
      name: "ask-what-would-be",
      summary: "See the field some steps ahead with a value placed first, the field itself untouched",
      steps: [
        { op: "field.branch", steps: 8, channel: 0, value: 1, place: [0, 0, 16, 1] },
      ],
    },
    {
      name: "ask-who-changed-a-cell",
      summary: "Place a value and ask who changed a cell, and what the last step did to it",
      steps: [
        { op: "field.place", channel: 0, value: 1, region: [10, 3, 3, 1] },
        { op: "field.explain", cell: [11, 3] },
      ],
    },
  ],
});
