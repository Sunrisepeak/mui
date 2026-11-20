// A row of controls: the arrows move the focus between the controls in it,
// Home and End to the first and the last. Its controls are anything.
import { defineContract } from "openmui/define";

export default defineContract({
  toolbar: {
    role: "toolbar",
    spec: "0.1",
    doc: "A row of controls; the arrows move the focus between them.",
    children: "any",
    props: {
      label: { type: "string", default: "", doc: "Its name." },
      vertical: { type: "bool", default: false, doc: "A column; the up and down arrows move." },
    },
    keys: { "Right|Down": "next", "Left|Up": "previous", Home: "first", End: "last" },
    commands: { next: {}, previous: {}, first: {}, last: {} },
    parts: { surface: {} },
  },
});
