// Words to search with: a text box with a magnifier before it and, while it
// holds words, a × that clears them; Enter searches, Escape clears. A search
// box to every reader.
import { defineContract } from "openmui/define";

export default defineContract({
  search_box: {
    role: "search_box",
    spec: "0.1",
    doc: "Words to search with: a magnifier, the words, a × while there are some; Enter searches.",
    props: {
      value: { type: "string", default: "", bindable: true },
      placeholder: { type: "string", default: "Search" },
      label: { type: "string", default: "Search", doc: "Its name." },
      disabled: { type: "bool", default: false },
    },
    events: { changed: { args: { value: "string" } }, submitted: { args: { value: "string" } } },
    actions: {
      set_value: { effect: "reversible", args: { value: "string" } },
      clear: { effect: "reversible", doc: "No words." },
      submit: { effect: "reversible", doc: "Search with the words (submitted)." },
    },
    parts: { icon: {}, field: {}, clear: {} },
  },
});
