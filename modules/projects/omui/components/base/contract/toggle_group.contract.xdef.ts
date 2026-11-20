// A few options side by side, each on or off on its own (bold, italic,
// underline): each a toggle button of its own (`<id>/<value>`), pressed, or
// turned by an agent; taken back.
import { defineContract } from "openmui/define";

export default defineContract({
  toggle_group: {
    role: "toolbar",
    spec: "0.1",
    doc: "A few options side by side, each on or off on its own; each a toggle button of its own.",
    props: {
      options: { type: "list<string>", default: [], doc: "The options' values; their labels from `labels` or the values." },
      labels: { type: "list<string>", default: [] },
      selected: { type: "list<string>", default: [], bindable: true, doc: "The options that are on." },
      label: { type: "string", default: "", doc: "The accessible name of the group." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { selected: "list<string>" } } },
    actions: { set_selected: { effect: "reversible", args: { selected: "list<string>" } } },
    keys: { "Right": "next", "Left": "previous" },
    commands: { next: { doc: "Focus the next option." }, previous: { doc: "Focus the option before." } },
    parts: { track: {}, option: {}, focus_ring: {} },
  },
});
