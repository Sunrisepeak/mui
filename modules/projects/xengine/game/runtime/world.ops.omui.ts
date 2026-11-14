// A game's operations as a subject of the agent kernel (D-162, D-173): what an
// Agent does with a game running on its own through xengine.runtime — see what
// a player is shown, play as a person plays, step and seek the game's time,
// ask what would be. omui-gen ops makes the runtime's table
// (src/catalog.cppm) and the listings (src/world.ops.json, world.ops.md);
// effects are openas's, admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "world",
  module: "xengine.runtime",
  namespace: "xengine::runtime",
  generated: "src",
  doc: "Observe a game running on its own, play it as a person does, step and seek its time, and ask what would be.",
  operations: [
    {
      name: "world.observe",
      effect: "read",
      summary: "What a player on this tier is shown at this frame, with the frame and the state's hash",
      result: "json",
    },
    {
      name: "world.state",
      effect: "read",
      summary: "The frame, the state's hash, whether the game is over, and the frames it can be sought within",
      result: "json",
    },
    {
      name: "world.act",
      effect: "reversible",
      summary: "A player's actions, as a person's: the state shows them from `effective`, no sooner than the tier's reaction after `seen`, and they hold until the next",
      params: [
        { name: "seen", type: "integer", doc: "the frame they were decided on (the latest played when none is given)", optional: true },
        { name: "buttons", type: "json", doc: "the buttons held, by index: [0]", optional: true },
        { name: "axis", type: "json", doc: "up to four axes, each -1 to 1: [1]", optional: true },
        { name: "pointer", type: "json", doc: "[x, y], each 0 to 1 of the game's area", space: "plane", optional: true },
      ],
      result: "json",
      undo: "world.seek to the frame before it took effect",
    },
    {
      name: "world.step",
      effect: "reversible",
      summary: "Plays steps; only where the agent steps the game (an owner's clock refuses it)",
      params: [{ name: "steps", type: "integer", doc: "how many (1 when none is given)", optional: true }],
      result: "json",
      cost: "bounded",
      undo: "world.seek to the frame it began at",
    },
    {
      name: "world.seek",
      examples: [{ frame: 0 }],
      effect: "reversible",
      summary: "Returns to a frame within the history kept; what is played from there is a new branch",
      params: [{ name: "frame", type: "integer", doc: "the frame to return to" }],
      result: "json",
      cost: "bounded",
      undo: "none within the runtime: what came after the frame is another branch's",
    },
    {
      name: "world.branch",
      effect: "read",
      summary: "What would be if given actions were given now: the future's view and hash some steps ahead — the actions taking effect as the tier's reaction allows, as `world.act` would have them; the game itself untouched",
      params: [
        { name: "steps", type: "integer", doc: "how far ahead (1 when none is given)", optional: true },
        { name: "buttons", type: "json", doc: "held from when the action takes effect, pressed as it does", optional: true },
        { name: "axis", type: "json", optional: true },
        { name: "pointer", type: "json", space: "plane", optional: true },
        { name: "then", type: "json", doc: "what follows: actions each with `after`, the step of the branch it is given at (a press and its release)", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
  tiers: [
    { name: "human", doc: "As a person plays: shown what is drawn, a tenth of a second's reaction, ten actions a second.", view: "drawn", reaction_ms: 100, actions_per_second: 10, operations: ["world.observe", "world.state", "world.act", "world.step", "world.seek", "world.branch"] },
    { name: "assist", doc: "Privileged: shown everything, no reaction time, no limit.", view: "full", reaction_ms: 0, actions_per_second: 0, operations: ["world.observe", "world.state", "world.act", "world.step", "world.seek", "world.branch"] },
  ],
  tasks: [
    {
      name: "look-at-the-game",
      summary: "Read what a player is shown and where the game's time is",
      steps: [
        { op: "world.observe" },
        { op: "world.state" },
      ],
    },
    {
      name: "play-a-moment",
      summary: "Hold an axis as a player does, play half a second, and look again",
      steps: [
        { op: "world.act", axis: [1] },
        { op: "world.step", steps: 30 },
        { op: "world.observe" },
      ],
    },
    {
      name: "ask-before-acting",
      summary: "See where an action would lead some steps ahead, the game itself untouched",
      steps: [
        { op: "world.branch", steps: 30, axis: [1] },
        { op: "world.state" },
      ],
    },
  ],
});
