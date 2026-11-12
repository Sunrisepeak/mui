// A soft body's operations as a subject of the agent kernel (D-173, D-174):
// what an Agent does with a body the XPBD family steps — read where it is and
// what it touches, push it, step it, return to a frame, ask what would be.
// omui-gen ops makes the table (src/agent/catalog.cppm) and the listings
// (src/agent/body.ops.json, body.ops.md); effects are openas's, admission is
// mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "body",
  module: "xengine.dynamics.agent",
  namespace: "xengine::dynamics::agent",
  generated: "src/agent",
  doc: "Read a soft body's place, squash, speed and contacts, push it, step it, return to a frame, and ask what would be.",
  operations: [
    {
      name: "body.observe",
      effect: "read",
      summary: "The frame, the state's hash, the body's centre, bounds, area against its rest area, its fastest particle's speed, and what it touches",
      params: [{ name: "budget", type: "integer", doc: "the most bytes of answer: contacts are left out from the last, and `more` says how many", optional: true }],
      result: "json",
    },
    {
      name: "body.particles",
      effect: "read",
      summary: "Each particle's place and velocity, in order around the ring",
      result: "json",
      cost: "bounded",
    },
    {
      name: "body.push",
      effect: "reversible",
      summary: "What acts on the body from the next step on, held until the next push: a force on the whole of it, a walk along what it stands on; and once, on the next step alone, a kick",
      params: [
        { name: "force", type: "json", doc: "[x, y], an acceleration added to gravity", optional: true },
        { name: "walk", type: "number", doc: "along the ground, signed", optional: true },
        { name: "kick", type: "json", doc: "[x, y], a velocity added once", optional: true },
      ],
      result: "json",
      undo: "body.seek to the frame before it took effect",
    },
    {
      name: "body.step",
      effect: "reversible",
      summary: "Steps the body under what is held",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "body.seek to the frame it began at",
    },
    {
      name: "body.seek",
      effect: "reversible",
      summary: "Returns to a frame within the history kept; what is stepped from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the body: what came after the frame is another branch's",
    },
    {
      name: "body.branch",
      effect: "read",
      summary: "What would be: the body some steps ahead under a given push, observed as body.observe does; the body itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "force", type: "json", optional: true },
        { name: "walk", type: "number", optional: true },
        { name: "kick", type: "json", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "body.contacts",
      effect: "read",
      summary: "What the last step touched, in words: its place, its squash, its speed, its contacts by kind, and the stages a step goes through",
      result: "string",
    },
  ],
});
