// One value typed or chosen (a searchable select): typing filters the
// options to those whose labels hold what is typed; arrows move among them,
// Enter or a press chooses, Escape closes. Each option an item of its own
// (`<id>/<value>`) an agent chooses; `free` lets a value typed that is not an
// option stand.
import { defineContract } from "openmui/define";

export default defineContract({
  combobox: {
    role: "combo_box",
    spec: "0.1",
    doc: "One value typed or chosen from the options matching what is typed.",
    props: {
      value: { type: "string", default: "", bindable: true },
      options: { type: "list<string>", default: [], doc: "Option values; labels from `labels` or the values." },
      labels: { type: "list<string>", default: [] },
      placeholder: { type: "string", default: "Search…" },
      free: { type: "bool", default: false, doc: "A value typed that is no option stands." },
      label: { type: "string", default: "" },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" } } },
    actions: { expand: { effect: "reversible" }, collapse: { effect: "reversible" }, set_value: { effect: "reversible", args: { value: "string" } } },
    parts: { field: {}, popup: {}, option: {} },
  },
});
