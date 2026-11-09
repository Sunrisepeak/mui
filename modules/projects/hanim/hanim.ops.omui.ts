// hanim's operations (convergence plan §10.3): what an Agent or a tool may do
// with a host's Engine through hanim.control. omui-gen ops makes the
// Controller's table (src/control/catalog.cppm) and the listings
// (src/control/hanim.ops.json for CLI and MCP, hanim.ops.md) from it; effects
// are openas's, admission is mlibs.control's.
import { defineOps } from "openmui/define";

export default defineOps({
  product: "hanim",
  module: "hanim.control",
  namespace: "hanim::control",
  doc: "Prepare, play, inspect and steer animations on a host's one Engine.",
  operations: [
    {
      name: "animation.prepare",
      effect: "read",
      summary: "Prepare an immutable animation plan",
      params: [{ name: "program", type: "program", doc: "the composition to prepare" }],
      result: "plan",
      cost: "bounded",
    },
    {
      name: "animation.play",
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
      effect: "read",
      summary: "Read one playback's transport state",
      params: [{ name: "playback", type: "playback" }],
      result: "json",
    },
    {
      name: "animation.plan",
      effect: "read",
      summary: "Read its immutable plan and channels",
      params: [{ name: "playback", type: "playback" }],
      result: "plan",
    },
    {
      name: "animation.seek",
      effect: "reversible",
      summary: "Sample absolute seconds",
      params: [{ name: "playback", type: "playback" }, { name: "position", type: "seconds", doc: "from its start" }],
      cost: "frame",
      undo: "seek back to the position animation.inspect read",
    },
    {
      name: "animation.seek-progress",
      effect: "reversible",
      summary: "Sample normalized progress",
      params: [{ name: "playback", type: "playback" }, { name: "progress", type: "number", doc: "0 to 1 (not for a program looping forever)" }],
      cost: "frame",
      undo: "seek back to the progress animation.inspect read",
    },
    {
      name: "animation.pause",
      effect: "reversible",
      summary: "Pause transport",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.resume",
    },
    {
      name: "animation.resume",
      effect: "reversible",
      summary: "Resume transport",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.pause",
    },
    {
      name: "animation.reverse",
      effect: "reversible",
      summary: "Reverse transport direction",
      params: [{ name: "playback", type: "playback" }],
      undo: "animation.reverse again",
    },
    {
      name: "animation.rate",
      effect: "reversible",
      summary: "Set a nonzero signed rate",
      params: [{ name: "playback", type: "playback" }, { name: "rate", type: "number", doc: "nonzero; negative plays backwards" }],
      undo: "animation.rate with the rate animation.inspect read",
    },
    {
      name: "animation.cancel",
      effect: "irreversible",
      summary: "Cancel a playback; optionally restore its initial pose",
      params: [{ name: "playback", type: "playback" }, { name: "restore", type: "bool", doc: "back to its first pose", optional: true }],
      undo: "none: a cancelled playback stays cancelled (play its program again)",
    },
    {
      name: "animation.release",
      effect: "irreversible",
      summary: "Release a playback and invalidate its handle",
      params: [{ name: "playback", type: "playback" }],
      undo: "none: the handle is gone (play its program again for a new one)",
    },
  ],
});
