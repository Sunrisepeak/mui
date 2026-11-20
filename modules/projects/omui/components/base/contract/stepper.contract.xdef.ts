// Where one is in a sequence of steps (a checkout, a setup): the steps done,
// the current one, the ones ahead; each step an item of its own
// (`<id>/<index>`), a done one gone back to by a press or an agent.
import { defineContract } from "openmui/define";

export default defineContract({
  stepper: {
    role: "navigation",
    spec: "0.1",
    doc: "Where one is among steps: those done, the current one, those ahead.",
    props: {
      steps: { type: "list<string>", default: [], doc: "The steps' names, in order." },
      value: { type: "i64", default: 0, bindable: true, doc: "The current step's index." },
      free: { type: "bool", default: false, doc: "Any step may be gone to, not only those done." },
      label: { type: "string", default: "Steps", doc: "The accessible name." },
      vertical: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "i64" } } },
    actions: {
      set_value: { effect: "reversible", args: { value: "i64" } },
      increment: { effect: "reversible", doc: "On to the next step (when free: the application moves it on when a step is done)." },
      decrement: { effect: "reversible", doc: "Back to the step before." },
    },
    keys: { "Left|Up": "decrement", "Right|Down": "increment" },
    parts: { marker: {}, connector: {}, label: {} },
  },
});
