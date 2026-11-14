// A sheet along the window's left or right edge, as tall as it, over the
// page (modal by default: the page dimmed behind it, Tab kept inside):
// navigation, filters, details. Escape, a press outside or its × closes it.
import { defineContract } from "openmui/define";

export default defineContract({
  drawer: {
    role: "dialog",
    spec: "0.1",
    children: "any",
    doc: "A sheet along the window's edge over the page, closed by Escape, a press outside or its ×.",
    props: {
      open: { type: "bool", default: false, bindable: true },
      title: { type: "string", default: "", doc: "Also its accessible name." },
      side: { type: "enum", values: ["start", "end"], default: "end", doc: "The window's left (start) or right (end) edge." },
      size: { type: "f64", default: 360, doc: "Its width." },
      modal: { type: "bool", default: true, doc: "The page dimmed behind it, and kept from the person while it is open." },
    },
    events: { closed: { args: {} } },
    actions: { dismiss: { effect: "reversible" } },
    keys: { Escape: "dismiss" },
    parts: { sheet: {}, title: {}, close: {}, scrim: {} },
  },
});
