// The interface of a component: the one source of truth. Its C++ base, the
// builder, the TS / Python / Lua stubs and the view-document schema are made
// from it when the package builds (component-binding §2).
import { defineContract } from "openmui/define";

export default defineContract({
  counter: {
    role: "spin_button",
    spec: "0.1",
    doc: "A number stepped up and down.",
    props: {
      value: { type: "i32", default: 0, bindable: true },
      step: { type: "i32", default: 1 },
      label: { type: "string", default: "", doc: "What it counts (the accessible name)." },
    },
    events: { changed: { args: { value: "i32" } } },
    actions: { increment: { effect: "reversible" }, decrement: { effect: "reversible" } },
    keys: { "Up|Right": "increment", "Down|Left": "decrement" },
  },
});
