// A 3D rigid world's operations as a subject of the agent kernel (D-173, D-174): what an Agent does with the
// bodies the rigid3 family (Jolt under this package's contract) steps — read where they are and what touched
// what, cast a ray, push a body, step, return to a frame, ask what would be. omui-gen ops makes the table
// (src/rigid3/agent/catalog.cppm) and the listings (src/rigid3/agent/rigid3.ops.json, rigid3.ops.md);
// effects are openas's, admission is mlibs.control's. A body is named by its id (a number); places are
// the world's metres, [x, y, z] with y up.
import { defineOps } from "omui/define";

export default defineOps({
  product: "rigid3",
  module: "xengine.dynamics.rigid3.agent",
  namespace: "xengine::dynamics::rigid3::agent",
  generated: "src/rigid3/agent",
  doc: "Read a 3D rigid world's bodies and what touched what, cast a ray into it, push a body, step it, return to a frame, and ask what would be.",
  operations: [
    {
      name: "rigid3.observe",
      effect: "read",
      summary: "The frame, the state's hash, how many bodies and how many awake, and what began and ended touching in the last step",
      params: [{ name: "budget", type: "integer", doc: "the most bytes of answer: contacts are left out from the last, and `more` says how many", optional: true }],
      result: "json",
    },
    {
      name: "rigid3.bodies",
      effect: "read",
      summary: "Each body's id, kind, place, turn, velocity and whether it is awake, in the order made",
      result: "json",
      cost: "bounded",
    },
    {
      name: "rigid3.ray",
      examples: [{ from: [0, 20, 0], to: [0, -20, 0] }],
      effect: "read",
      summary: "The first body a segment meets: which, how far along, where and the surface's normal there",
      params: [
        { name: "from", type: "json", doc: "[x, y, z]" },
        { name: "to", type: "json", doc: "[x, y, z]" },
      ],
      result: "json",
    },
    {
      name: "rigid3.push",
      then: [{ why: "step the world: the push acts on the next step", request: { op: "rigid3.step" } }],
      examples: [{ body: 1, impulse: [0, 400, 0] }],
      effect: "reversible",
      summary: "An impulse on a body, given on the next step: through its centre of mass, or at a point in the world",
      params: [
        { name: "body", type: "integer", doc: "the body's id" },
        { name: "impulse", type: "json", doc: "[x, y, z], in newton seconds" },
        { name: "at", type: "json", doc: "[x, y, z], where in the world it is given (its centre of mass when none is given)", optional: true },
      ],
      result: "json",
      undo: "rigid3.seek to the frame before it took effect",
    },
    {
      name: "rigid3.step",
      then: [{ why: "read what happened", request: { op: "rigid3.observe" } }],
      effect: "reversible",
      summary: "Steps the world",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "rigid3.seek to the frame it began at",
    },
    {
      name: "rigid3.seek",
      then: [{ why: "read the world at the frame returned to", request: { op: "rigid3.observe" } }],
      examples: [{ frame: 120 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept, bit for bit (a checkpoint and the pushes since, played again); what is stepped from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the world: what came after the frame is another branch's",
    },
    {
      name: "rigid3.branch",
      examples: [{ steps: 30, body: 1, impulse: [0, 400, 0] }],
      effect: "read",
      summary: "What would be: the world some steps ahead, a body pushed first if one is given, observed as rigid3.observe does; the world itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "body", type: "integer", optional: true },
        { name: "impulse", type: "json", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
  tasks: [
    {
      name: "see-what-is-there",
      summary: "Read the bodies, then what lies straight down from above",
      steps: [
        { op: "rigid3.observe" },
        { op: "rigid3.bodies" },
        { op: "rigid3.ray", from: [0, 20, 0], to: [0, -20, 0] },
      ],
    },
    {
      name: "push-and-watch",
      summary: "Push a body, step the world, and read what it struck",
      steps: [
        { op: "rigid3.push", body: 1, impulse: [0, 400, 0] },
        { op: "rigid3.step", steps: 30 },
        { op: "rigid3.observe" },
      ],
    },
    {
      name: "ask-before-pushing",
      summary: "See what a push would do some steps ahead, the world itself untouched",
      steps: [
        { op: "rigid3.branch", steps: 30, body: 1, impulse: [0, 400, 0] },
        { op: "rigid3.observe" },
      ],
    },
  ],
});
