// A date chosen from a month's calendar: the field shows it (or asks for
// one); its popup is a month, each day an item an agent finds and activates
// (`<id>/calendar/<yyyy-mm-dd>`), the months before and after a press away.
// Arrows move a day or a week, PageUp/PageDown a month, Enter chooses.
// Dates are ISO (2026-11-02); `min` and `max` bound them.
import { defineContract } from "openmui/define";

export default defineContract({
  date_picker: {
    role: "combo_box",
    spec: "0.1",
    doc: "A date chosen from a month's calendar.",
    props: {
      value: { type: "string", default: "", bindable: true, doc: "The date, ISO: 2026-11-02; empty: none yet." },
      min: { type: "string", default: "", doc: "The earliest date that may be chosen (ISO)." },
      max: { type: "string", default: "", doc: "The latest (ISO)." },
      label: { type: "string", default: "" },
      placeholder: { type: "string", default: "Pick a date" },
      week_start: { type: "enum", values: ["monday", "sunday"], default: "monday" },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" } } },
    actions: { expand: { effect: "reversible" }, collapse: { effect: "reversible" }, set_value: { effect: "reversible", args: { value: "string" } } },
    keys: { "Space|Enter|Alt+Down": "expand", Escape: "collapse" },
    parts: { field: {}, calendar: {}, day: {} },
  },
});
