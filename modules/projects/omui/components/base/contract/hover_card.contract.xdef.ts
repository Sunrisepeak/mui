// More about the element before it, shown while the person points at it or
// focuses it (after `delay`) and while the pointer is over the card itself;
// what it holds is anything. Not modal: Escape or `dismiss` closes it.
import { defineContract } from "openmui/define";

export default defineContract({
  hover_card: {
    role: "dialog",
    spec: "0.1",
    doc: "More about the element before it, shown while it is pointed at or focused, and while the pointer is over the card.",
    children: "any",
    props: {
      open: { type: "bool", default: false, bindable: true },
      delay: { type: "f64", default: 0.5, doc: "Seconds of pointing before it shows." },
      linger: { type: "f64", default: 0.3, doc: "Seconds it stays after the pointer leaves (to reach the card)." },
      label: { type: "string", default: "", doc: "Its name." },
    },
    actions: { show: { effect: "reversible" }, dismiss: { effect: "reversible" } },
    keys: { Escape: "dismiss" },
    parts: { surface: {} },
  },
});
