// The air's operations as a subject of the agent kernel (D-173, D-174; the M6.5 design's T5.5): what an Agent
// does with a field of air — read its cells' warmth, wetness and charge and the wind, apply a quantity at a place
// (a fire's heat held, a gust of wetness once), step it, return to a frame, ask what would be, and why a cell is
// what it is. ops-gen makes the table (src/agent/catalog.cppm) and the listings (src/agent/air.ops.json,
// air.ops.md); effects are openas's, admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "air",
  module: "xengine.field.agent",
  namespace: "xengine::field::agent",
  generated: "src/agent",
  doc: "Read the air's cells and wind, apply a quantity at a place, step it, return to a frame, ask what would be, and why a cell is what it is.",
  operations: [
    {
      name: "air.observe",
      effect: "read",
      summary: "The frame, the state's hash, the cells and parked blocks, the strongest wind, and each channel over the field: its unit, least, most and total, and where its most is",
      params: [{ name: "budget", type: "integer", doc: "the most bytes of answer: channels are left out from the last, and `more` says how many", optional: true }],
      result: "json",
    },
    {
      name: "air.at",
      effect: "read",
      summary: "The air at a place: the cell there (none outside the cells), each channel in its unit, how warm in kelvin, and the wind",
      params: [{ name: "at", type: "json", doc: "[x, y, z] in metres" }],
      result: "json",
      examples: [{ at: [0.5, 1.5, 0.5] }],
    },
    {
      name: "air.set",
      then: [{ why: "step the air: what is applied acts from the next step", request: { op: "air.step" } }],
      effect: "reversible",
      summary: "A quantity applied to the cell at a place at the start of the next step — once, or held there every step until the next air.set of that cell and channel (a fire that keeps burning)",
      params: [
        { name: "at", type: "json", doc: "[x, y, z] in metres: a cell must be there" },
        { name: "channel", type: "string", doc: "a channel of the program (temperature, wetness, charge …)" },
        { name: "value", type: "number", doc: "in the channel's unit (temperature: kelvin)" },
        { name: "hold", type: "boolean", doc: "applied every step, not once (false lets a held one go)", optional: true },
      ],
      result: "json",
      undo: "air.seek to the frame before it took effect",
      examples: [{ at: [0.5, 0.5, 0.5], channel: "temperature", value: 900, hold: true }],
    },
    {
      name: "air.step",
      then: [{ why: "read the air now", request: { op: "air.observe" } }],
      effect: "reversible",
      summary: "Steps the air: what is applied, then the laws across the cells, then the wind moved and the heat carried",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "air.seek to the frame it began at",
    },
    {
      name: "air.seek",
      then: [{ why: "read the air at the frame returned to", request: { op: "air.observe" } }],
      examples: [{ frame: 60 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept; what is stepped from there is a new branch, and what was held is let go",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the air: what came after the frame is another branch's",
    },
    {
      name: "air.branch",
      effect: "read",
      summary: "What would be: the air some steps ahead with what is held and, if given, one more quantity applied, observed as air.observe does; the air itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "set", type: "json", doc: "{at, channel, value, hold?}: as air.set, in the branch alone", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "air.explain",
      keywords: ["why", "cause", "reason", "source"],
      effect: "read",
      summary: "Why the cell at a place is what it is after the last step: each law that held, what flowed in and out by which law from which neighbour (its place), what it read, and the material it became by, if it did",
      params: [{ name: "at", type: "json", doc: "[x, y, z] in metres" }],
      result: "json",
      examples: [{ at: [0.5, 1.5, 0.5] }],
    },
  ],
  tasks: [
    {
      name: "see-the-air",
      summary: "Read the field as a whole and the air at one place",
      steps: [
        { op: "air.observe" },
        { op: "air.at", at: [0.5, 1.5, 0.5] },
      ],
    },
    {
      name: "light-a-fire",
      summary: "Hold a cell hot, step, and read the updraft and why the cell above is warm",
      steps: [
        { op: "air.set", at: [0.5, 0.5, 0.5], channel: "temperature", value: 900, hold: true },
        { op: "air.step", steps: 60 },
        { op: "air.at", at: [0.5, 3.5, 0.5] },
        { op: "air.explain", at: [0.5, 1.5, 0.5] },
      ],
    },
    {
      name: "ask-before-heating",
      summary: "See what a fire would do some steps ahead, the air itself untouched",
      steps: [
        { op: "air.branch", steps: 60, set: { at: [0.5, 0.5, 0.5], channel: "temperature", value: 900, hold: true } },
        { op: "air.observe" },
      ],
    },
  ],
});
