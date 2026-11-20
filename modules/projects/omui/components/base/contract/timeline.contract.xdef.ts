// A course of steps on a shape of its own (G8, D-253): where a replay, a
// history or a film is, moved by the person or an agent. The shape is given:
// a line (the default), a ring (the site draws it on its mark's ring) or any
// path; the steps lie along it at equal lengths. Its track, the part played,
// each step's mark and the playhead are its parts, carried by the style's
// motion. Pressed or dragged, the nearest point of the shape takes the
// nearest step; the keys and an agent move it as a slider's value. Over
// another node (`over`), it lies on that node's own shape where it is drawn.
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
      line: {
        type: "bool",
        default: true,
        doc: "Its track and the part played drawn; false: only the marks and the playhead, on a shape its owner draws itself (the site's O).",
      },
      view_box: {
        type: "string",
        default: "",
        doc: "The shape's own box (x y width height), laid as a path lays it: its ratio kept, centred. Empty: a 1×1 box stretched to its own.",
      },
      over: {
        type: "string",
        default: "",
        doc: "A node (its path) whose box the course lies in, not its own: the steps on that node's shape where it is (a replay's course on the site's O); only a press near the course is its.",
      },
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
