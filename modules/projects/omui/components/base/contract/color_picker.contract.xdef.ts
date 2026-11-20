// A colour: a swatch that opens a picker — a field of saturation and
// brightness, a hue strip, and the application's own swatches, each an item
// an agent finds (`<id>/hue`, `<id>/swatch_<n>`); or set at
// once as #rrggbb. A drag is taken back as one change.
import { defineContract } from "openmui/define";

export default defineContract({
  color_picker: {
    role: "combo_box",
    spec: "0.1",
    doc: "A colour chosen from a field and a hue strip, or the application's swatches.",
    props: {
      value: { type: "string", default: "#4f46e5", bindable: true, doc: "#rrggbb." },
      swatches: { type: "list<string>", default: [], doc: "Colours offered at once (#rrggbb)." },
      label: { type: "string", default: "" },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" }, coalesce: "frame" } },
    actions: { expand: { effect: "reversible" }, collapse: { effect: "reversible" }, set_value: { effect: "reversible", args: { value: "string" } } },
    keys: { "Space|Enter|Alt+Down": "expand", Escape: "collapse" },
    parts: { swatch: {}, field: {}, hue: {} },
  },
});
