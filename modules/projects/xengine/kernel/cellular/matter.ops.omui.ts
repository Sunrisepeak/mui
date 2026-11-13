// A field of matter's operations as a subject of the agent kernel (D-169, D-173, D-174): what an Agent does
// with matter on the lattice in matter's own words — materials by name, channels in their units, laws by name
// and by their place in the definition — beside the field's own operations (field.ops.omui.ts), which keep
// time: field.step, field.seek. omui-gen ops makes the table (src/matter/agent/catalog.cppm) and the listings
// (matter.ops.json, matter.ops.md); effects are openas's, admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "matter",
  module: "xengine.cellular.matter.agent",
  namespace: "xengine::cellular::matter_agent",
  generated: "src/matter/agent",
  doc: "Read a field of matter by material and in units, place a material, apply a quantity, ask why a cell is what it is, what happened, and what would.",
  operations: [
    {
      name: "matter.describe",
      effect: "read",
      summary: "What the world is made of: its materials, its channels with their units, its laws by name, kind and place in the definition; one material's properties or one law in full when named",
      params: [
        { name: "material", type: "string", doc: "this material's properties: what differs from the columns' defaults", optional: true },
        { name: "law", type: "string", doc: "this law in full: its condition as written, what it changes", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "matter.observe",
      effect: "read",
      summary: "A region as matter: how many cells of each material, each channel in its unit (least, most, mean), how many cells are in each named state (burning)",
      params: [
        { name: "region", type: "json", doc: "[x, y, width, height] in cells (the whole field when none is given)", space: "lattice-2d", optional: true },
        { name: "budget", type: "integer", doc: "the most bytes of answer: channels, then materials, are left out from the last, and `more` says how many", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "matter.at",
      effect: "read",
      summary: "One cell: its material and phase, its channels in their units, and its properties where its state leaves them other than written (wet: a higher ignition)",
      params: [{ name: "cell", type: "json", doc: "[x, y]", space: "lattice-2d" }],
      result: "json",
      cost: "bounded",
    },
    {
      name: "matter.place",
      effect: "reversible",
      summary: "Makes a region of a material anew — every channel as a new instance of it has — in one frame, recorded with who placed it",
      params: [
        { name: "material", type: "string", doc: "its name" },
        { name: "region", type: "json", doc: "[x, y, width, height] in cells", space: "lattice-2d" },
      ],
      result: "json",
      cost: "bounded",
      undo: "field.seek to the frame before it",
    },
    {
      name: "matter.apply",
      effect: "reversible",
      summary: "A quantity applied to a region — a channel set to, raised by or lowered by an amount in its unit (heat: temperature add 400) — in one frame, recorded with who applied it",
      params: [
        { name: "channel", type: "string", doc: "its name: temperature, wetness, charge, pressure …" },
        { name: "set", type: "number", doc: "to this, in the channel's unit", optional: true },
        { name: "add", type: "number", doc: "raised by this", optional: true },
        { name: "take", type: "number", doc: "lowered by this", optional: true },
        { name: "region", type: "json", doc: "[x, y, width, height] in cells", space: "lattice-2d" },
      ],
      result: "json",
      cost: "bounded",
      undo: "field.seek to the frame before it",
    },
    {
      name: "matter.explain",
      effect: "read",
      summary: "Why a cell is what it is: what is on record of it, the latest first — the law that held (its name, its condition, its place in the definition), what the law read, and the neighbours' parts: who gave what it needed, who took it away, what was beside it when something ran short, each followed to its own cause and to who placed or applied what — and, for a law named, whether it holds now and which comparison fails",
      params: [
        { name: "cell", type: "json", doc: "[x, y]", space: "lattice-2d" },
        { name: "law", type: "string", doc: "judge this law for the cell as it is now: held, or what fails", optional: true },
        { name: "depth", type: "integer", doc: "how far back along the givers' own causes (2 when none is given; 0 … 6)", optional: true },
        { name: "budget", type: "integer", doc: "the most bytes of answer: older records are left out, and `more` says how many", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "matter.events",
      effect: "read",
      summary: "What happened, oldest first: cells that became another material (by which law), named states begun and ended (ignite, go_out), edits and their actors — since a frame, of a name, in a region",
      params: [
        { name: "since", type: "integer", doc: "records of frames after this one (all kept when none is given)", optional: true },
        { name: "name", type: "string", doc: "only events of this name: ignite, boil, melt, placed …", optional: true },
        { name: "region", type: "json", doc: "only what happened here", space: "lattice-2d", optional: true },
        { name: "budget", type: "integer", doc: "the most bytes of answer: the latest are kept, and `more` says how many older are left out", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "matter.branch",
      effect: "read",
      summary: "What would be: the field some steps ahead — a material placed or a quantity applied first when given — as the materials of a region with their change, and the events that would happen, counted by name; the field itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "place", type: "json", doc: "{material, region}: placed before the steps", space: "lattice-2d", optional: true },
        { name: "apply", type: "json", doc: "{channel, set | add | take, region}: applied before the steps", optional: true },
        { name: "region", type: "json", doc: "the region observed (the whole field when none is given)", space: "lattice-2d", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
});
