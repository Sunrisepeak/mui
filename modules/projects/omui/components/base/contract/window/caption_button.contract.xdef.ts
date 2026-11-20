// One of a window frame's buttons (D-75): minimize, maximize, restore or
// close, as a square glyph button (`standard`) or a coloured circle
// (`traffic`). Activating it invokes its window.* verb.
import { defineContract } from "openmui/define";

export default defineContract({
  caption_button: {
    role: "button",
    spec: "0.1",
    doc: "A window frame's button: minimize, maximize, restore or close.",
    props: {
      kind: { type: "enum", values: ["minimize", "maximize", "restore", "close"], default: "close", doc: "What it does." },
      look: { type: "enum", values: ["standard", "traffic"], default: "standard", doc: "A square glyph button, or a coloured circle." },
      active: { type: "bool", default: true, doc: "Whether its window is active (an inactive window's circles are grey)." },
      label: { type: "string", default: "", doc: "The accessible name (default: what it does, in the person's language)." },
    },
    events: {
      activated: { args: {} },
    },
    actions: {
      activate: { effect: "reversible" },
    },
    keys: {
      "Enter|Space": "activate",
    },
    parts: {
      background: {},
      glyph: {},
    },
    motion: {
      hover: "motion.fast",
    },
  },
});
