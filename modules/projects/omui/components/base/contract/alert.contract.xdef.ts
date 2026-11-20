// A message in the page about something that needs attention — inline, not
// over a corner (that is a toast): a tone, a title, the message, one action
// at most, and dismissible when the application lets it be.
import { defineContract } from "openmui/define";

export default defineContract({
  alert: {
    role: "status",
    spec: "0.1",
    doc: "A message in the page that needs attention: a tone, a title, the message, one action at most.",
    props: {
      title: { type: "string", default: "" },
      message: { type: "string", default: "", doc: "With the title, its accessible name." },
      tone: { type: "enum", values: ["info", "success", "warning", "danger"], default: "info" },
      action: { type: "string", default: "", doc: "Its one action's label; none: no action." },
      dismissible: { type: "bool", default: false },
      open: { type: "bool", default: true, bindable: true },
    },
    events: { acted: { args: {} }, dismissed: { args: {} } },
    actions: {
      activate: { effect: "reversible", doc: "Its action (when it has one)." },
      dismiss: { effect: "reversible", doc: "Closes it (when dismissible)." },
    },
    parts: { surface: {}, icon: {}, title: {}, message: {}, action: {}, close: {} },
  },
});
