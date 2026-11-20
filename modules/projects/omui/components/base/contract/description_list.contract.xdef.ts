// Terms and what each means, in pairs ("Plan" — "Team, 29 a month"): a
// definition list; each pair an item of its own to every reader, its name
// the term and its value the description. Stacked, or terms beside their
// descriptions in an aligned column.
import { defineContract } from "openmui/define";

export default defineContract({
  description_list: {
    role: "list",
    spec: "0.1",
    doc: "Terms and what each means, in pairs; each pair an item of its own.",
    props: {
      terms: { type: "list<string>", default: [], bindable: true },
      details: { type: "list<string>", default: [], bindable: true, doc: "Each term's description, in the same order." },
      aligned: { type: "bool", default: true, doc: "Terms in a column beside their descriptions; else each above its own." },
      label: { type: "string", default: "", doc: "The accessible name of the list." },
    },
    parts: { term: {}, detail: {}, divider: {} },
  },
});
