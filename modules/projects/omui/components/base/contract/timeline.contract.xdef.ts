// A course of steps on a shape of its own (G8, D-253): where a replay, a
// history or a film is, moved by the person or an agent. The shape is given:
// a line (the default), a ring (the site draws it on its mark's ring) or any
// path; the steps lie along it at equal lengths. Its track, the part played,
// each step's mark and the playhead are its parts, carried by the style's
// motion. Pressed or dragged, the nearest point of the shape takes the
// nearest step; the keys and an agent move it as a slider's value.
import { defineContract } from "openmui/define";

export default defineContract({
  timeline: {
    role: "slider",
    spec: "0.1",
    doc: "Where one is along a course of steps, on a shape of its own (a line, a ring, any path): pressed, dragged, keyed or set by an agent.",
    props: {
      steps: { type: "i64", default: 2, doc: "How many steps (two at least)." },
      value: { type: "f64", default: 1, bindable: true, doc: "The step it is at: 1 (the first) to steps." },
      shape: {
        type: "string",
        default: "line",
        doc: "line, ring (from the top, clockwise), or SVG path data in a 1×1 box (M0 0.5 L1 0.5), scaled to its own.",
      },
      captions: { type: "list<string>", default: [], doc: "Each step's words, said when it is at that step (none: step n of m)." },
      label: { type: "string", default: "Timeline", doc: "The accessible name." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "f64" } } },
    actions: {
      set_value: { effect: "reversible", args: { value: "f64" }, doc: "To a step, by its number (1 … steps)." },
      increment: { effect: "reversible", doc: "On to the next step." },
      decrement: { effect: "reversible", doc: "Back to the step before." },
    },
    keys: { "Right|Up": "increment", "Left|Down": "decrement" },
    parts: { track: {}, played: {}, mark: {}, playhead: {} },
  },
});
