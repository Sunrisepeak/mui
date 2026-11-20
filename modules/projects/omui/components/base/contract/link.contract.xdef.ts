// Text that goes somewhere: an address the system opens (its browser, its
// mail) or a verb of the application's; underlined, in the accent. Its
// effect is external when it leaves the application (its address: an agent
// answers "yes" to follow it), else its verb's.
import { defineContract } from "openmui/define";

export default defineContract({
  link: {
    role: "link",
    spec: "0.1",
    doc: "Text that goes somewhere: an address the system opens, or a verb of the application's.",
    props: {
      text: { type: "string", default: "", bindable: true, doc: "What it says, also its name." },
      icon: { type: "string", default: "", bindable: true,
              doc: "An icon before its text, or alone where it has none (its name then its `label`): SVG path data in a 24-unit square, as icon sets draw them, filled in the link's colour." },
      href: { type: "string", default: "", bindable: true, doc: "The address (http, https, mailto) the system opens." },
      verb: { type: "string", default: "", doc: "Instead, the application's verb it invokes." },
      disabled: { type: "bool", default: false },
    },
    events: { activated: { args: { href: "string" } } },
    actions: { activate: { effect: "reversible", doc: "Follow it: the address opened (external), or the verb invoked (its effect)." } },
    keys: { Enter: "activate" },
    parts: { icon: {}, label: {}, underline: {}, focus_ring: {} },
  },
});
