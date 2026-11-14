// A 3D rigid world's operations as a subject of the agent kernel (D-173, D-174): what an Agent does with the
// bodies the rigid3 family (Jolt under this package's contract) steps — read where they are and what touched
// what, cast a ray or a shape, push a body, drive a joint's motor, walk and jump a character, step, return to a
// frame, ask what would be. omui-gen ops makes the table
// (src/rigid3/agent/catalog.cppm) and the listings (src/rigid3/agent/rigid3.ops.json, rigid3.ops.md);
// effects are openas's, admission is mlibs.control's. A body, a joint and a character are each named by its id
// (a number); places are the world's metres, [x, y, z] with y up.
import { defineOps } from "omui/define";

export default defineOps({
  product: "rigid3",
  module: "xengine.dynamics.rigid3.agent",
  namespace: "xengine::dynamics::rigid3::agent",
  generated: "src/rigid3/agent",
  doc: "Read a 3D rigid world's bodies, joints, characters and what touched what, cast a ray or a shape into it, push a body, drive a joint's motor, walk and jump a character, step it, return to a frame, and ask what would be.",
  operations: [
    {
      name: "rigid3.observe",
      effect: "read",
      summary: "The frame, the state's hash, how many bodies (and how many awake), joints and characters, and what began and ended touching in the last step (a sensor's overlaps marked `sensor`)",
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
      name: "rigid3.cast",
      examples: [{ shape: "sphere", radius: 0.5, from: [0, 20, 0], to: [0, -20, 0] }],
      effect: "read",
      summary: "The first body a shape swept along a segment meets: which, how far along, where and the surface's normal there",
      params: [
        { name: "shape", type: "string", doc: "sphere (the default), box or capsule", optional: true },
        { name: "radius", type: "number", doc: "sphere, capsule: m (0.5 when none is given)", optional: true },
        { name: "half", type: "json", doc: "box: [x, y, z], its half extents", optional: true },
        { name: "half_height", type: "number", doc: "capsule: half the length of its middle, along y (0.5 when none is given)", optional: true },
        { name: "from", type: "json", doc: "[x, y, z], where the shape's centre starts" },
        { name: "to", type: "json", doc: "[x, y, z], where it would end" },
      ],
      result: "json",
    },
    {
      name: "rigid3.joints",
      effect: "read",
      summary: "Each joint's id, form, bodies (0: the world), value (a hinge's angle, a slider's place, a distance joint's length) and motor, in the order made",
      result: "json",
      cost: "bounded",
    },
    {
      name: "rigid3.drive",
      then: [{ why: "step the world: the motor drives from the next step", request: { op: "rigid3.step" } }],
      examples: [{ joint: 1, mode: "velocity", target: 2 }],
      effect: "reversible",
      summary: "A hinge's or a slider's motor, from the next step: off, toward a speed, or toward an angle or a place",
      params: [
        { name: "joint", type: "integer", doc: "the joint's id" },
        { name: "mode", type: "string", doc: "off, velocity (the default) or position", optional: true },
        { name: "target", type: "number", doc: "rad/s or m/s (velocity), rad or m (position)", optional: true },
      ],
      result: "json",
      undo: "rigid3.seek to the frame before it took effect",
    },
    {
      name: "rigid3.move",
      then: [{ why: "step the world: the character walks from the next step", request: { op: "rigid3.step" } }],
      examples: [{ character: 1, velocity: [2, 0, 0] }],
      effect: "reversible",
      summary: "A character's walk, from the next step until told otherwise: up steps and the slopes it can climb, stuck to the floor going down",
      params: [
        { name: "character", type: "integer", doc: "the character's id" },
        { name: "velocity", type: "json", doc: "[x, y, z] in m/s; y is left to gravity and jumps" },
      ],
      result: "json",
      undo: "rigid3.seek to the frame before it took effect",
    },
    {
      name: "rigid3.jump",
      then: [{ why: "step the world and see where it lands", request: { op: "rigid3.step", steps: 60 } }],
      examples: [{ character: 1, speed: 5 }],
      effect: "reversible",
      summary: "A character's jump on the next step, when it stands on the ground (the answer says whether it does now)",
      params: [
        { name: "character", type: "integer", doc: "the character's id" },
        { name: "speed", type: "number", doc: "m/s upward (5 when none is given)", optional: true },
      ],
      result: "json",
      undo: "rigid3.seek to the frame before it took effect",
    },
    {
      name: "rigid3.where",
      examples: [{ character: 1 }],
      effect: "read",
      summary: "Where a character is (or every one): its feet, its velocity and walk, and what it stands on",
      params: [{ name: "character", type: "integer", doc: "the character's id (every one when none is given)", optional: true }],
      result: "json",
    },
    {
      name: "rigid3.ground",
      effect: "read",
      summary: "What each character stood on after the last step, in the order of their ids: on the ground, on steep ground, not supported or in the air, and which body",
      result: "json",
      cost: "bounded",
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
      name: "walk-a-character",
      summary: "Walk a character, step, and read where it stands",
      steps: [
        { op: "rigid3.where" },
        { op: "rigid3.move", character: 1, velocity: [2, 0, 0] },
        { op: "rigid3.step", steps: 60 },
        { op: "rigid3.ground" },
      ],
    },
    {
      name: "turn-a-motor",
      summary: "Read the joints, drive a hinge's motor, step, and read its angle",
      steps: [
        { op: "rigid3.joints" },
        { op: "rigid3.drive", joint: 1, mode: "velocity", target: 2 },
        { op: "rigid3.step", steps: 60 },
        { op: "rigid3.joints" },
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
