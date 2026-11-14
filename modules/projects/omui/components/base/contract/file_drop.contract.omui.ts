// Files given to the application: dragged from the system and let go over it
// (it says so while they are over it), or chosen — pressing it asks the
// application for the system's chooser (`requested`). What it does not accept
// is left out and said. An agent gives files with `drop`.
import { defineContract } from "openmui/define";

export default defineContract({
  file_drop: {
    role: "button",
    spec: "0.1",
    doc: "Files given to the application: dropped on it, or chosen (pressing it asks for the chooser).",
    props: {
      label: { type: "string", default: "Drop files here, or choose", doc: "What it says; also its name." },
      accept: { type: "list<string>", default: [], doc: "The extensions it takes (\"png\", \"jpg\"); none: any file." },
      multiple: { type: "bool", default: true, doc: "Several files at once; else the first." },
      disabled: { type: "bool", default: false },
    },
    events: { dropped: { args: { files: "list<string>" } }, requested: { args: {} } },
    actions: {
      drop: { effect: "reversible", args: { files: "list<string>" }, doc: "Give it files (paths); what it does not accept is refused." },
      activate: { effect: "reversible", doc: "Ask the application for the system's chooser." },
    },
    keys: { "Enter|Space": "activate" },
    parts: { zone: {}, label: {}, focus_ring: {} },
  },
});
