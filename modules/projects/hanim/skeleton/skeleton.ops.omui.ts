// Characters' skeletal animation as a subject of the agent kernel (D-173, D-174): what an Agent does with a crowd
// of characters hanim.skeleton plays — read the rig and each character's state, send a character to a state or
// set its machine's parameters, layer a clip on part of it, ask where a joint is, sample any clip at any time,
// step, return to a frame, ask what would be. omui-gen ops makes the table (src/agent/catalog.cppm) and the
// listings (src/agent/skeleton.ops.json, skeleton.ops.md); effects are openas's, admission is mlibs.control's. A
// character is named by its index (from 0), a clip, a state and a joint by name; places are metres, [x, y, z]
// with y up.
import { defineOps } from "omui/define";

export default defineOps({
  product: "skeleton",
  module: "hanim.skeleton.agent",
  namespace: "hanim::skeleton::agent",
  generated: "src/agent",
  doc: "Play skeletal characters: read the rig and each character's state, send one to a state or set its parameters, layer a clip on part of it, ask where a joint is, sample any clip at any time, step, return to a frame, and ask what would be.",
  operations: [
    {
      name: "skeleton.observe",
      effect: "read",
      summary: "The frame, the state's hash, the rig (joints; clips with their length, raw and compressed bytes and compression error; the machine's states and parameters) and each character: its state, how far its fade is, the last transition and why, its parameters, layers and where it stands",
      params: [
        { name: "budget", type: "integer", doc: "the most bytes of answer; characters are left out from the last, and `more` says how many", optional: true },
      ],
      result: "json",
    },
    {
      name: "skeleton.play",
      then: [{ why: "see the fade begin", request: { op: "skeleton.observe" } }],
      examples: [{ character: 0, state: "run", fade: 0.3 }],
      effect: "reversible",
      summary: "Sends a character to a state of its machine now, fading over some seconds (no condition asked)",
      params: [
        { name: "character", type: "integer" },
        { name: "state", type: "string", doc: "a state of the machine (skeleton.observe lists them)" },
        { name: "fade", type: "number", doc: "seconds (0.2 when none is given)", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "skeleton.seek to the frame it was given at",
    },
    {
      name: "skeleton.set",
      examples: [{ character: 0, parameter: "speed", value: 3 }],
      effect: "reversible",
      summary: "Sets a parameter of a character's machine (a trigger: 1); its transitions read it from the next step",
      params: [
        { name: "character", type: "integer" },
        { name: "parameter", type: "string" },
        { name: "value", type: "number" },
      ],
      result: "json",
      cost: "bounded",
      undo: "skeleton.seek to the frame it was given at",
    },
    {
      name: "skeleton.blend",
      examples: [{ character: 0, layer: 0, clip: "wave", weight: 1, mask: "upper_chest" }],
      effect: "reversible",
      summary: "Layers a clip over what the machine plays: mixed in by a weight (on a joint's subtree when a mask is given) or added (its difference from its own start); a weight of 0 takes the layer away",
      params: [
        { name: "character", type: "integer" },
        { name: "layer", type: "integer", doc: "0 … 3 (0 when none is given)", optional: true },
        { name: "clip", type: "string", doc: "a clip of the rig", optional: true },
        { name: "weight", type: "number", doc: "0 … 1" },
        { name: "mask", type: "string", doc: "the joint whose subtree it plays on (every joint when none is given)", optional: true },
        { name: "additive", type: "boolean", doc: "added rather than mixed (false when none is given)", optional: true },
        { name: "speed", type: "number", doc: "1 when none is given", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "skeleton.blend with weight 0, or skeleton.seek",
    },
    {
      name: "skeleton.joint",
      examples: [{ character: 0, joint: "hand_r" }],
      effect: "read",
      summary: "Where a joint is: in the world (the character's place, then the joint in the model's space), its turn and its place relative to the model",
      params: [
        { name: "character", type: "integer" },
        { name: "joint", type: "string" },
      ],
      result: "json",
    },
    {
      name: "skeleton.sample",
      examples: [{ clip: "walk", time: 0.5, joint: "foot_l" }],
      effect: "read",
      summary: "Any clip at any time, as a pure function of it: a joint's place in the model (or every joint's, within a budget), the root motion from the clip's start, and the error compression left there",
      params: [
        { name: "clip", type: "string" },
        { name: "time", type: "number", doc: "seconds (clamped to the clip)" },
        { name: "joint", type: "string", doc: "every joint when none is given", optional: true },
        { name: "budget", type: "integer", doc: "every joint: the most bytes of answer", optional: true },
      ],
      result: "json",
    },
    {
      name: "skeleton.step",
      then: [{ why: "see where the characters got to", request: { op: "skeleton.observe" } }],
      examples: [{ steps: 60 }],
      effect: "reversible",
      summary: "Steps every character (1/60 s a step): machines choose, clips play, root motion carries them",
      params: [{ name: "steps", type: "integer", doc: "1 when none is given", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "skeleton.seek to the frame it began at",
    },
    {
      name: "skeleton.seek",
      then: [{ why: "read the characters at the frame returned to", request: { op: "skeleton.observe" } }],
      examples: [{ frame: 60 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept, bit for bit (a checkpoint and the inputs since, given again); what is stepped from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the world: what came after the frame is another branch's",
    },
    {
      name: "skeleton.branch",
      examples: [{ steps: 60, character: 0, state: "run" }],
      effect: "read",
      summary: "What would be: the characters some steps ahead, one sent to a state first if one is given, observed as skeleton.observe does; the characters themselves untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "character", type: "integer", optional: true },
        { name: "state", type: "string", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
  tasks: [
    {
      name: "read-the-rig",
      summary: "Read the rig and the characters, then sample a clip where a foot lands",
      steps: [
        { op: "skeleton.observe" },
        { op: "skeleton.sample", clip: "walk", time: 0.5, joint: "foot_r" },
      ],
    },
    {
      name: "make-one-run",
      summary: "Set a character's speed, step through the fade, and check where its hand is and that it moved forward",
      steps: [
        { op: "skeleton.set", character: 0, parameter: "speed", value: 3 },
        { op: "skeleton.step", steps: 60 },
        { op: "skeleton.observe" },
        { op: "skeleton.joint", character: 0, joint: "hand_r" },
      ],
    },
    {
      name: "wave-while-walking",
      summary: "Ask what a wave on the upper body would look like before giving it: a branch ahead, then the layer and a step",
      steps: [
        { op: "skeleton.branch", steps: 30 },
        { op: "skeleton.blend", character: 1, layer: 0, clip: "wave", weight: 1, mask: "upper_chest" },
        { op: "skeleton.step", steps: 30 },
        { op: "skeleton.joint", character: 1, joint: "hand_r" },
      ],
    },
  ],
});
