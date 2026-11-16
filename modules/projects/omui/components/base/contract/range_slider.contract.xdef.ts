// A range in a range: two thumbs on one track (a price from–to), the low
// never past the high. Each thumb a slider of its own in the semantic tree
// (`<id>/low`, `<id>/high`); dragged, stepped by the arrows (Tab between
// them), set by an agent, the drag taken back as one action.
import { defineContract } from "openmui/define";

export default defineContract({
  range_slider: {
    role: "group",
    spec: "0.1",
    doc: "A range chosen with two thumbs on one track, the low never past the high.",
    props: {
      low: { type: "f64", default: 0, bindable: true },
      high: { type: "f64", default: 100, bindable: true },
      min: { type: "f64", default: 0 },
      max: { type: "f64", default: 100 },
      step: { type: "f64", default: 1 },
      label: { type: "string", default: "" },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { low: "f64", high: "f64" }, coalesce: "frame" }, committed: { args: { low: "f64", high: "f64" } } },
    actions: { set_range: { effect: "reversible", args: { low: "f64", high: "f64" } } },
    keys: { "Right|Up": "increment", "Left|Down": "decrement" },
    commands: { increment: { doc: "Step the focused thumb up." }, decrement: { doc: "Step the focused thumb down." } },
    parts: { track: {}, fill: {}, thumb: {} },
  },
});
