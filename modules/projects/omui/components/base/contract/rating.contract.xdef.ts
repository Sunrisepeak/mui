// How much, out of a few stars: a slider to every reader (0 … max, in whole
// or half steps). The pointer shows what a press would set before it sets it;
// pressing the value it has clears it. Read-only, it only shows.
import { defineContract } from "openmui/define";

export default defineContract({
  rating: {
    role: "slider",
    spec: "0.1",
    doc: "How much, out of a few stars; a slider to every reader.",
    props: {
      value: { type: "f64", default: 0, bindable: true, doc: "0 (none) … max." },
      max: { type: "i64", default: 5, doc: "How many stars." },
      half: { type: "bool", default: false, doc: "Half stars: steps of 0.5." },
      label: { type: "string", default: "", doc: "What is rated (its name)." },
      readonly: { type: "bool", default: false, doc: "It only shows." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "f64" } } },
    actions: {
      increment: { effect: "reversible" },
      decrement: { effect: "reversible" },
      set_value: { effect: "reversible", args: { value: "f64 | max" } },
    },
    keys: { "Right|Up": "increment", "Left|Down": "decrement", Home: "set_value(0)", End: "set_value(max)" },
    parts: { star: {}, focus_ring: {} },
  },
});
