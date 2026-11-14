// Several words or names gathered as tags (labels, recipients, topics):
// typed and kept with Enter or a comma, chosen from the suggestions matching
// what is typed, each tag a chip removed by its ×. An agent adds and removes
// them by name; each change taken back.
import { defineContract } from "openmui/define";

export default defineContract({
  tag_input: {
    role: "group",
    spec: "0.1",
    doc: "Tags gathered by typing or choosing a suggestion, each removed by its ×.",
    props: {
      tags: { type: "list<string>", default: [], bindable: true },
      suggestions: { type: "list<string>", default: [], doc: "Offered as one types (those holding what is typed)." },
      placeholder: { type: "string", default: "Add…" },
      free: { type: "bool", default: true, doc: "A tag that is no suggestion may be added." },
      label: { type: "string", default: "" },
      disabled: { type: "bool", default: false },
    },
    events: { added: { args: { tag: "string" } }, removed: { args: { tag: "string" } } },
    actions: {
      add: { effect: "reversible", args: { tag: "string" } },
      remove: { effect: "reversible", args: { tag: "string" } },
      set_tags: { effect: "reversible", args: { tags: "any" } },
    },
    parts: { tag: {}, field: {}, popup: {} },
  },
});
