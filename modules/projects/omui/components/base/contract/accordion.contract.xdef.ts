// A section that opens and closes: its header always shown, its content only
// while expanded (a disclosure, WAI-ARIA's pattern: the header a button that
// says whether it is expanded). How it opens is the slot's (universal motion).
import { defineContract } from "openmui/define";

export default defineContract({
  accordion: {
    children: "any",
    role: "region",
    spec: "0.1",
    doc: "A section whose header is always shown and whose content shows while it is expanded.",
    props: {
      title: { type: "string", default: "", bindable: true, doc: "The header, also the accessible name." },
      expanded: { type: "bool", default: false, bindable: true },
      disabled: { type: "bool", default: false },
    },
    events: { toggled: { args: { expanded: "bool" } } },
    actions: {
      expand: { effect: "reversible" },
      collapse: { effect: "reversible" },
      activate: { effect: "reversible", doc: "Expands it when collapsed, collapses it when expanded." },
    },
    keys: { "Enter|Space": "activate" },
    parts: { header: {}, chevron: {}, panel: {} },
  },
});
