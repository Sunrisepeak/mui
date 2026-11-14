// hanim's operations (convergence plan §10.3): what an Agent or a tool may do
// with a host's Engine through hanim.control. omui-gen ops makes the
// Controller's table (src/control/catalog.cppm) and the listings
// (src/control/hanim.ops.json for CLI and MCP, hanim.ops.md) from it; effects
// are openas's, admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "hanim",
  module: "hanim.control",
  namespace: "hanim::control",
  doc: "Prepare, play, inspect and steer animations on a host's one Engine.",
  operations: [
    {
      name: "animation.prepare",
      examples: [{ program: "rise" }],
      effect: "read",
      summary: "Prepare an immutable animation plan",
      params: [{ name: "program", type: "program", doc: "the composition to prepare" }],
      result: "plan",
      cost: "bounded",
    },
    {
      name: "animation.play",
      examples: [{ program: "rise" }],
      effect: "reversible",
      summary: "Start an authored or prepared animation",
      params: [{ name: "program", type: "program", doc: "a composition or a prepared plan" }],
      result: "playback",
      cost: "bounded",
      undo: "animation.cancel with restore puts its objects back",
    },
    { name: "animation.list", effect: "read", summary: "List owned playback identities and transport state", result: "json" },
    {
      name: "animation.inspect",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "read",
      summary: "Read one playback's transport state",
      params: [{ name: "playback", type: "playback" }],
      result: "json",
    },
    {
      name: "animation.plan",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "read",
      summary: "Read its immutable plan and channels",
      params: [{ name: "playback", type: "playback" }],
      result: "plan",
    },
    {
      name: "animation.seek",
      examples: [{ playback: { slot: 0, generation: 1 }, position: 0.4 }],
      effect: "reversible",
      summary: "Sample absolute seconds",
      params: [{ name: "playback", type: "playback" }, { name: "position", type: "seconds", doc: "from its start" }],
      cost: "frame",
      undo: "seek back to the position animation.inspect read",
    },
    {
      name: "animation.seek-progress",
      examples: [{ playback: { slot: 0, generation: 1 }, progress: 0.5 }],
      effect: "reversible",
      summary: "Sample normalized progress",
      params: [{ name: "playback", type: "playback" }, { name: "progress", type: "number", doc: "0 to 1 (not for a program looping forever)" }],
      cost: "frame",
      undo: "seek back to the progress animation.inspect read",
    },
    {
      name: "animation.pause",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "reversible",
      summary: "Pause transport",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.resume",
    },
    {
      name: "animation.resume",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "reversible",
      summary: "Resume transport",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.pause",
    },
    {
      name: "animation.reverse",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "reversible",
      summary: "Reverse transport direction",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.reverse again",
    },
    {
      name: "animation.rate",
      examples: [{ playback: { slot: 0, generation: 1 }, rate: 2 }],
      effect: "reversible",
      summary: "Set a nonzero signed rate",
      params: [{ name: "playback", type: "playback" }, { name: "rate", type: "number", doc: "nonzero; negative plays backwards" }],
      undo: "animation.rate with the rate animation.inspect read",
    },
    {
      name: "animation.cancel",
      examples: [{ playback: { slot: 0, generation: 1 }, restore: true }],
      effect: "irreversible",
      summary: "Cancel a playback; optionally restore its initial pose",
      params: [{ name: "playback", type: "playback" }, { name: "restore", type: "bool", doc: "back to its first pose", optional: true }],
      undo: "none: a cancelled playback stays cancelled (play its program again)",
    },
    {
      name: "animation.release",
      examples: [{ playback: { slot: 0, generation: 1 } }],
      effect: "irreversible",
      summary: "Release a playback and invalidate its handle",
      params: [{ name: "playback", type: "playback" }],
      undo: "none: the handle is gone (play its program again for a new one)",
    },
  ],
  tasks: [
    {
      name: "see-what-is-playing",
      summary: "List the playbacks here: each one's handle (what every other operation takes as `playback`), its program and where its transport is",
      steps: [
        { op: "animation.list" },
      ],
    },
    {
      name: "inspect-a-playback",
      summary: "List the playbacks and read the first one whole: its transport, tracks and segments (`$last` takes its handle from the list's answer)",
      steps: [
        { op: "animation.list" },
        { op: "animation.inspect", playback: { $last: "/playbacks/0/playback" } },
      ],
    },
    {
      name: "scrub-to-the-middle",
      summary: "Put the first playback at half its length and see where its transport is",
      steps: [
        { op: "animation.list" },
        { op: "animation.seek-progress", playback: { $last: "/playbacks/0/playback" }, progress: 0.5 },
        { op: "animation.list" },
      ],
    },
    {
      name: "pause-a-playback",
      summary: "Pause the first playback and see its state",
      steps: [
        { op: "animation.list" },
        { op: "animation.pause", playback: { $last: "/playbacks/0/playback" } },
        { op: "animation.list" },
      ],
    },
  ],
});
