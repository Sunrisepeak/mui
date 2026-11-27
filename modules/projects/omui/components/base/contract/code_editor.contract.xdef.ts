// Code, edited: a multi-line field in the code style, its text coloured by
// what each stretch is (the language's words, strings, numbers, comments),
// its lines numbered; Tab indents (Escape, then Tab, leaves it), Enter keeps
// the line's indent. Its text is its field's (`<id>/text`), read and edited
// by an agent as any text box is; set whole with set_value, taken back.
import { defineContract } from "openmui/define";

export default defineContract({
  code_editor: {
    role: "group",
    spec: "0.1",
    doc: "Code edited: coloured, its lines numbered, Tab indenting, Enter keeping the indent.",
    props: {
      text: { type: "string", default: "", bindable: true },
      language: { type: "enum", values: ["cpp", "typescript", "python", "lua", "json", "toml", "plain"], default: "cpp" },
      label: { type: "string", default: "" },
      readonly: { type: "bool", default: false },
      lines: { type: "i64", default: 8, doc: "The lines it is tall enough for (more grow it)." },
    },
    events: { changed: { args: { text: "string" }, coalesce: "frame" } },
    actions: { set_value: { effect: "reversible", args: { value: "string" }, doc: "Its whole text." } },
    parts: { gutter: {}, text: {}, language: {} },
  },
});
