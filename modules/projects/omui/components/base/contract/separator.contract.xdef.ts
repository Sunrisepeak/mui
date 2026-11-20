// A line between groups of content, across or down; with a word in its
// middle ("or") when it has one. A separator to every reader.
import { defineContract } from "openmui/define";

export default defineContract({
  separator: {
    role: "separator",
    spec: "0.1",
    doc: "A line between groups of content, across or down, with a word in its middle when it has one.",
    props: {
      vertical: { type: "bool", default: false, doc: "Down, between things side by side; else across." },
      label: { type: "string", default: "", bindable: true, doc: "A word in the line's middle (\"or\"); also its name." },
    },
    parts: { line: {}, label: {} },
  },
});
