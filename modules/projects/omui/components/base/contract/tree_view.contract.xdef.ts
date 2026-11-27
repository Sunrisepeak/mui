// A hierarchy (files, a table of contents, an organisation) from a model's
// rows, each naming its parent (its `parent` field; none: a root) and its
// label (`label`, else `title`, else its key). Each row an item of its own
// (`<id>/<key>`), a tree item expanded and collapsed, chosen by a press, the
// arrows (Right opens, Left closes or goes up) or an agent. Rows it holds
// are drawn as they come into view.
import { defineContract } from "openmui/define";

export default defineContract({
  tree_view: {
    role: "tree",
    spec: "0.1",
    doc: "A hierarchy from a model's rows, each a tree item opened, closed and chosen.",
    props: {
      model: { type: "string", default: "", doc: "The model whose rows it shows: each row's `parent` field names its parent's key." },
      value: { type: "string", default: "", bindable: true, doc: "The chosen row's key." },
      expanded: { type: "list<string>", default: [], bindable: true, doc: "The keys of the rows open." },
      label: { type: "string", default: "" },
    },
    events: { changed: { args: { value: "string" } }, activated: { args: { key: "string" } } },
    actions: {
      set_value: { effect: "reversible", args: { value: "string" } },
      set_expanded: { effect: "reversible", args: { key: "string", open: "bool" } },
    },
    keys: { Down: "next", Up: "previous", Right: "open", Left: "close", "Enter|Space": "activate" },
    commands: {
      next: { doc: "Choose the row below." },
      previous: { doc: "Choose the row above." },
      open: { doc: "Open the chosen row, or go to its first child." },
      close: { doc: "Close the chosen row, or go to its parent." },
      activate: { doc: "Activate the chosen row." },
    },
    parts: { row: {}, toggle: {}, label: {} },
  },
});
