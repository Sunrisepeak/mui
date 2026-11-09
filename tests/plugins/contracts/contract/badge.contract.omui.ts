// A count with a tone, that can be dismissed: a component of a package that
// is not openmui's (it has no privilege: component-binding §1).
import { defineContract } from "openmui/define";

export default defineContract({
  badge: {
    role: "status",
    spec: "0.1",
    doc: "A small count with a tone; dismissing it clears it.",
    props: {
      count: { type: "i32", default: 0, bindable: true, doc: "What it counts; 0 hides the number." },
      tone: { type: "enum", values: ["neutral", "good", "bad"], default: "neutral" },
      label: { type: "string", default: "", doc: "What the count is of (the accessible name)." },
    },
    events: { cleared: { args: { was: "i32" } } },
    actions: { dismiss: { effect: "reversible" } },
    keys: { Escape: "dismiss" },
    parts: { pill: {} },
  },
});
