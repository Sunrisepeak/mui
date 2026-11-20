// A surface holding one thing's content: a title, a subtitle and whatever it
// is given. Raised from the page (elevated) or outlined; one that can be
// activated is a button to every reader, as a press anywhere on it is.
import { defineContract } from "openmui/define";

export default defineContract({
  card: {
    children: "any",
    role: "group",
    spec: "0.1",
    doc: "A surface holding one thing: a title, a subtitle and its content; activated as a whole when it is interactive.",
    props: {
      title: { type: "string", default: "", bindable: true, doc: "Also the accessible name." },
      subtitle: { type: "string", default: "", bindable: true },
      variant: { type: "enum", values: ["elevated", "outlined", "filled"], default: "elevated" },
      interactive: { type: "bool", default: false, doc: "The whole card can be activated (a link to what it shows)." },
      selected: { type: "bool", default: false, bindable: true },
    },
    events: { activated: { args: {} } },
    actions: { activate: { effect: "reversible" } },
    keys: { "Enter|Space": "activate" },
    parts: { surface: {}, title: {}, subtitle: {} },
  },
});
