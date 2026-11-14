// A time of day in its parts: the hours and the minutes, each a spin
// button of its own (`<id>/hours`, `<id>/minutes`) stepped by the arrows or
// typed; Left and Right go between them. Kept on `step` minutes. The value
// is HH:MM, 24-hour; shown in 12 when `hour12`.
import { defineContract } from "openmui/define";

export default defineContract({
  time_picker: {
    role: "group",
    spec: "0.1",
    doc: "A time of day, its hours and minutes each stepped or typed.",
    props: {
      value: { type: "string", default: "09:00", bindable: true, doc: "HH:MM, 24-hour." },
      step: { type: "i64", default: 1, doc: "Minutes a step moves, and the minutes kept on." },
      hour12: { type: "bool", default: false, doc: "Shown with AM and PM." },
      label: { type: "string", default: "" },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" } } },
    actions: { set_value: { effect: "reversible", args: { value: "string" } } },
    keys: { Up: "increment", Down: "decrement", Left: "previous", Right: "next" },
    commands: {
      increment: { doc: "Step the focused part up." },
      decrement: { doc: "Step the focused part down." },
      previous: { doc: "Focus the part before." },
      next: { doc: "Focus the part after." },
    },
    parts: { field: {}, hours: {}, minutes: {}, period: {} },
  },
});
