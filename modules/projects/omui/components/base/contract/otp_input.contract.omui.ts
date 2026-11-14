// A one-time code, a box for each character: typed one by one (each box
// moving on), pasted whole, taken back with Backspace; `completed` when every
// box holds one. A text box to every reader, its value the code.
import { defineContract } from "openmui/define";

export default defineContract({
  otp_input: {
    role: "text_box",
    spec: "0.1",
    doc: "A one-time code, a box for each character; a text box to every reader.",
    props: {
      value: { type: "string", default: "", bindable: true },
      length: { type: "i64", default: 6, doc: "How many characters." },
      numeric: { type: "bool", default: true, doc: "Digits only; else letters and digits." },
      label: { type: "string", default: "", doc: "Its name (\"Verification code\")." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" } }, completed: { args: { value: "string" } } },
    actions: { set_value: { effect: "reversible", args: { value: "string" }, doc: "The whole code; what it cannot hold is refused." } },
    parts: { cell: {}, caret: {}, focus_ring: {} },
  },
});
