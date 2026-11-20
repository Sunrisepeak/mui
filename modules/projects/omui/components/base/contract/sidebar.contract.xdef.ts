// The places of an application down its side: each a link of its own
// (`<id>/<value>`), the current one marked; collapsed, only each place's
// first letter in a narrow rail. Chosen by a press, the arrows and Enter, or
// an agent; taken back.
import { defineContract } from "openmui/define";

export default defineContract({
  sidebar: {
    role: "navigation",
    spec: "0.1",
    doc: "The places of an application down its side; each a link of its own, the current one marked.",
    props: {
      items: { type: "list<string>", default: [], doc: "The places' values; their labels from `labels` or the values." },
      labels: { type: "list<string>", default: [] },
      selected: { type: "string", default: "", bindable: true, doc: "The current place." },
      collapsed: { type: "bool", default: false, bindable: true, doc: "A narrow rail of first letters." },
      label: { type: "string", default: "", doc: "Its name." },
    },
    events: { changed: { args: { selected: "string" } } },
    actions: {
      set_selected: { effect: "reversible", args: { selected: "string" } },
      expand: { effect: "reversible" },
      collapse: { effect: "reversible" },
    },
    keys: { Down: "next", Up: "previous" },
    commands: { next: { doc: "Choose the next place." }, previous: { doc: "Choose the place before." } },
    parts: { surface: {}, item: {}, current: {}, focus_ring: {} },
  },
});
