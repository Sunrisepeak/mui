// A cloud of particles as a subject of the agent kernel (D-173, D-174): what an Agent does with things that fly
// in 3D as a carrier of matter — read what is in flight and what struck what, find those near a place, emit
// more, step, return to a frame, ask what would be. omui-gen ops makes the table (src/agent/catalog.cppm) and
// the listings (src/agent/particles.ops.json, particles.ops.md); effects are openas's, admission is
// mlibs.control's. A particle is named by its id (a number, in the order emitted); a material by its name in
// the world's definition of matter; places are metres, [x, y, z] with y up.
import { defineOps } from "omui/define";

export default defineOps({
  product: "particles",
  module: "xengine.particles.agent",
  namespace: "xengine::particles::agent",
  generated: "src/agent",
  doc: "Read a cloud of particles in flight — where each is, what it is made of, how hot — and what struck the world, find those near a place, emit more, step it, return to a frame, and ask what would be.",
  operations: [
    {
      name: "particles.observe",
      effect: "read",
      summary: "The frame, the cloud's fingerprint, how many are in flight (by material), what struck the world in the last step (fastest first) and how many things matter's laws did",
      params: [{ name: "budget", type: "integer", doc: "the most bytes of answer: strikes are left out from the slowest, and `more` says how many", optional: true }],
      result: "json",
    },
    {
      name: "particles.list",
      examples: [{ first: 0, most: 16 }],
      effect: "read",
      summary: "Particles in the order emitted: id, material, place, velocity, age, life left and temperature in kelvin",
      params: [
        { name: "first", type: "integer", doc: "from this place in the order (0 when none is given)", optional: true },
        { name: "most", type: "integer", doc: "at most this many (64 when none is given; 1024 at most)", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "particles.near",
      examples: [{ at: [0, 0, 0], radius: 0.5 }],
      effect: "read",
      summary: "The particles within a distance of a place, the nearest first, as particles.list gives them",
      params: [
        { name: "at", type: "json", doc: "[x, y, z]" },
        { name: "radius", type: "number", doc: "m (1 when none is given)", optional: true },
        { name: "most", type: "integer", doc: "at most this many (64 when none is given; 1024 at most)", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "particles.emit",
      then: [{ why: "step the cloud: they are emitted on the next step", request: { op: "particles.step", steps: 30 } }],
      examples: [{ at: [0, 1, 0], velocity: [0, 3, 0], material: "iron", kelvin: 1500, count: 20, spread: 1 }],
      effect: "reversible",
      summary: "Particles of a material emitted on the next step: at a place, at a speed — `count` of them, their speeds spread by up to `spread` m/s each way, the same spread every run",
      params: [
        { name: "at", type: "json", doc: "[x, y, z]" },
        { name: "velocity", type: "json", doc: "[x, y, z] in m/s ([0, 0, 0] when none is given)", optional: true },
        { name: "material", type: "string", doc: "a material of the world's definition (particles.observe's `materials` lists those in flight)" },
        { name: "kelvin", type: "number", doc: "how hot (the material's own start when none is given)", optional: true },
        { name: "life", type: "number", doc: "seconds (1 when none is given)", optional: true },
        { name: "size", type: "number", doc: "m across (0.02 when none is given)", optional: true },
        { name: "count", type: "integer", doc: "how many (1 when none is given; 4096 at most)", optional: true },
        { name: "spread", type: "number", doc: "m/s each way added to each one's velocity (0 when none is given)", optional: true },
      ],
      result: "json",
      undo: "particles.seek to the frame before it took effect",
    },
    {
      name: "particles.step",
      then: [{ why: "read what happened", request: { op: "particles.observe" } }],
      effect: "reversible",
      summary: "Steps the cloud: flight, the world's surfaces met, one tick of matter's laws",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "particles.seek to the frame it began at",
    },
    {
      name: "particles.seek",
      then: [{ why: "read the cloud at the frame returned to", request: { op: "particles.observe" } }],
      examples: [{ frame: 60 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept, bit for bit (a copy of the cloud and the emissions since, played again); what is stepped from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the cloud: what came after the frame is another branch's",
    },
    {
      name: "particles.branch",
      examples: [{ steps: 60, at: [0, 1, 0], velocity: [0, 3, 0], material: "iron", kelvin: 1500, count: 20, spread: 1 }],
      effect: "read",
      summary: "What would be: the cloud some steps ahead, particles emitted first if `at` and `material` are given (as particles.emit takes them), observed as particles.observe does; the cloud itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "at", type: "json", optional: true },
        { name: "velocity", type: "json", optional: true },
        { name: "material", type: "string", optional: true },
        { name: "kelvin", type: "number", optional: true },
        { name: "life", type: "number", optional: true },
        { name: "size", type: "number", optional: true },
        { name: "count", type: "integer", optional: true },
        { name: "spread", type: "number", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
  tasks: [
    {
      name: "see-what-is-in-flight",
      summary: "Read the cloud, then the first particles and those near the middle",
      steps: [
        { op: "particles.observe" },
        { op: "particles.list", first: 0, most: 16 },
        { op: "particles.near", at: [0, 0, 0], radius: 1 },
      ],
    },
    {
      name: "throw-sparks",
      summary: "Emit hot iron sparks, step a second, and read what struck the world and how hot they are now",
      steps: [
        { op: "particles.emit", at: [0, 1, 0], velocity: [0, 3, 0], material: "iron", kelvin: 1500, count: 20, spread: 1 },
        { op: "particles.step", steps: 60 },
        { op: "particles.observe" },
        { op: "particles.list", most: 4 },
      ],
    },
    {
      name: "ask-before-emitting",
      summary: "See what an emission would do a second ahead, the cloud itself untouched",
      steps: [
        { op: "particles.branch", steps: 60, at: [0, 1, 0], velocity: [0, 3, 0], material: "iron", kelvin: 1500, count: 20, spread: 1 },
        { op: "particles.observe" },
      ],
    },
  ],
});
