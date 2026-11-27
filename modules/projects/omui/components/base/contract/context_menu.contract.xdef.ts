// The choices for the element before it, where the person asked for them: a
// secondary press (right-click, a long press) over it opens the menu at the
// pointer, the Menu key or Shift+F10 at the element; its items are
// menu_items. Escape, a press outside or a choice closes it, focus given back.
import { defineContract } from "openmui/define";

export default defineContract({
  context_menu: {
    role: "menu",
    spec: "0.1",
    doc: "The choices for the element before it, opened where the person asked for them; its items are menu_items.",
    children: "menu_item",
    props: {
      open: { type: "bool", default: false, bindable: true },
      label: { type: "string", default: "", doc: "Its name." },
    },
    events: { closed: { args: {} } },
    actions: {
      show: { effect: "reversible", args: { x: "f64", y: "f64" }, doc: "Open it at a point of the window (none: at its element)." },
      dismiss: { effect: "reversible", doc: "Close it." },
    },
    keys: { Escape: "dismiss" },
    parts: { surface: {} },
  },
});
