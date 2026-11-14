// A number one types or steps: the digits typed (Enter or leaving keeps them,
// within min and max, on the step), the buttons and the arrow keys step it.
import { defineContract } from "openmui/define";

export default defineContract({
  number_input: {
    role: "spin_button",
    spec: "0.1",
    doc: "A number one types or steps, kept within min and max on its step.",
    props: {
      value: { type: "f64", default: 0, bindable: true },
      min: { type: "f64", default: -1e+300 },
      max: { type: "f64", default: 1e+300 },
      step: { type: "f64", default: 1 },
      digits: { type: "i64", default: 0, doc: "Digits after the point shown." },
      label: { type: "string", default: "", doc: "The accessible name." },
      suffix: { type: "string", default: "", doc: "A unit shown after the number (px, %, kg)." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "f64" }, coalesce: "frame" }, committed: { args: { value: "f64" } } },
    actions: {
      increment: { effect: "reversible" },
      decrement: { effect: "reversible" },
      set_value: { effect: "reversible", args: { value: "f64 | min | max" } },
    },
    keys: { Up: "increment", Down: "decrement", PageUp: "increment", PageDown: "decrement", Home: "set_value(min)", End: "set_value(max)" },
    parts: { field: {}, digits: {}, decrement: {}, increment: {} },
  },
});
