// The application's commands found by name: over the window's middle, a
// field for words and the verbs that match them (those that take no
// arguments, or the ones it is given); arrows choose, Enter runs, Escape
// closes. Each command an item of its own an agent can see and run.
import { defineContract } from "openmui/define";

export default defineContract({
  command_palette: {
    role: "dialog",
    spec: "0.1",
    doc: "The application's commands found by name; arrows choose, Enter runs.",
    props: {
      open: { type: "bool", default: false, bindable: true },
      query: { type: "string", default: "", bindable: true, doc: "The words the commands are found by." },
      verbs: { type: "list<string>", default: [], doc: "The verbs it offers; none: every verb that takes no arguments." },
      placeholder: { type: "string", default: "Type a command" },
      label: { type: "string", default: "Commands", doc: "Its name." },
    },
    events: { ran: { args: { verb: "string" } }, closed: { args: {} } },
    actions: {
      show: { effect: "reversible" },
      dismiss: { effect: "reversible" },
      set_query: { effect: "reversible", args: { query: "string" } },
    },
    keys: { Down: "next", Up: "previous", Enter: "run", Escape: "dismiss" },
    commands: { next: { doc: "Choose the next command." }, previous: { doc: "Choose the command before." }, run: { doc: "Run the chosen command." } },
    parts: { surface: {}, field: {}, command: {}, chosen: {} },
  },
});
