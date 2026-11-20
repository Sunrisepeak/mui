// A compact label a person presses, turns on and off (a filter), or removes
// (a tag): a button, or a toggle button when selectable; its remove button
// an item of its own (`<id>/remove`) when removable.
import { defineContract } from "openmui/define";

export default defineContract({
  chip: {
    role: "button",
    spec: "0.1",
    doc: "A compact label: pressed, turned on and off as a filter, or removed as a tag.",
    props: {
      text: { type: "string", default: "", doc: "Also the accessible name." },
      selectable: { type: "bool", default: false, doc: "A filter: pressing turns it on and off (a toggle button)." },
      selected: { type: "bool", default: false, bindable: true },
      removable: { type: "bool", default: false, doc: "A tag: an × that removes it (the application takes it out)." },
      disabled: { type: "bool", default: false },
    },
    events: { activated: { args: {} }, toggled: { args: { selected: "bool" } }, removed: { args: {} } },
    actions: { activate: { effect: "reversible" }, remove: { effect: "reversible", doc: "Removes it (when removable): emits removed." } },
    keys: { "Space|Enter": "activate", "Delete|Backspace": "remove" },
    parts: { surface: {}, label: {}, check: {}, remove: {} },
  },
});
