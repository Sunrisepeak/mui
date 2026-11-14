// A control with what it is for: its label above, a line of help under it,
// and in place of the help an error when there is one. The control inside
// takes the label as its name and the help or the error as its description;
// required, it says so.
import { defineContract } from "openmui/define";

export default defineContract({
  form_field: {
    role: "group",
    spec: "0.1",
    doc: "A control with its label, its help, its error; the control named by the label.",
    children: "any",
    props: {
      label: { type: "string", default: "", bindable: true },
      help: { type: "string", default: "", bindable: true, doc: "A line under the control." },
      error: { type: "string", default: "", bindable: true, doc: "What is wrong, in place of the help; none: nothing." },
      required: { type: "bool", default: false },
    },
    parts: { label: {}, help: {}, error: {} },
  },
});
