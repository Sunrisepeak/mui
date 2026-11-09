// Live Design's operations (D-149, convergence plan W14): what an Agent does
// with an application started in development mode (`omui dev`) — read its
// sources and API, edit them live, set and check what the person expects, hand
// the floor over and keep what they did as a test, and write the edits back to
// the TypeScript sources. They are the application's verbs (`ui.invoke`), the
// player implements them; omui-gen ops makes the table (src/catalog/design.cppm)
// the player registers them from and MCP serves them by, and the listings
// (src/catalog/design.ops.json, design.ops.md). Effects are openas's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "design",
  module: "omui.agent",
  partition: "design",
  namespace: "omui::agent::design",
  doc: "Design a running application live: its sources and API, edits run at once with their state kept, expectations checked, the person's turn kept as a test, edits written back to the TypeScript sources.",
  generated: "src/catalog",
  operations: [
    {
      name: "design.sources",
      effect: "read",
      summary: "The scripts the application is made of: each one's path, language and the text it runs now (edited, or its file's), and the modules they import",
      result: "json",
      cost: "bounded",
    },
    {
      name: "design.api",
      effect: "read",
      summary: "How the scripts are written here: each language's API over this application's components — every builder with its properties (types, choices), handlers and actions, and the functions of the module — read before writing an edit",
      result: "json",
      cost: "bounded",
    },
    {
      name: "design.locate",
      effect: "read",
      summary: "Between a node and its source, either way: given `node`, the line of the script that built it (its path as design.sources names it, line, column, the line's text); given `path` and `line`, the nodes built on that line",
      params: [
        { name: "node", type: "node", doc: "a node's id: where it was built", optional: true },
        { name: "path", type: "string", doc: "a script or module, as design.sources names it", optional: true },
        { name: "line", type: "integer", doc: "a line of it, from 1: the nodes built there", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "design.edit",
      effect: "reversible",
      summary: "Runs new text as a script, at once, its state kept: the semantic changes it made and how long it took — or its problems, with their lines, and nothing changed. The edit stays until design.commit writes it to the file or design.discard drops it",
      params: [
        { name: "path", type: "string", doc: "a script or a module it imports, as design.sources names it" },
        { name: "text", type: "string", doc: "its whole new text" },
      ],
      result: "json",
      cost: "bounded",
      undo: "design.edit with the text before, or design.discard",
    },
    {
      name: "design.set",
      effect: "reversible",
      summary: "Sets properties of a node by editing the builder call that built it (design.locate): each value written where the property is, or added to its object — the rest of the source as it was — then run as design.edit runs an edit: the semantic changes, and the lines it changed",
      params: [
        { name: "node", type: "node", doc: "a node a script built" },
        { name: "props", type: "json", doc: "{name: value} as its contract names them (snapshot, design.api); null takes one out" },
      ],
      result: "json",
      cost: "bounded",
      undo: "design.set with the values before, or design.discard",
    },
    {
      name: "design.insert",
      effect: "reversible",
      summary: "Adds an element, written as the script's language writes it, among the children of a node's builder call (at an index, or last), laid out as its siblings are; run as design.edit runs an edit",
      params: [
        { name: "parent", type: "node", doc: "the node whose builder call gets the child" },
        { name: "code", type: "string", doc: "the element: text({ id: \"help\", text: \"Help\" })" },
        { name: "index", type: "integer", doc: "before the child at this index (last when absent)", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "design.remove the node it added, or design.discard",
    },
    {
      name: "design.remove",
      effect: "reversible",
      summary: "Takes a node's builder call out of the list of children it is in (with its comma, and its line when it had one); run as design.edit runs an edit. A node built outside a list (a function's result, a condition) is refused: edit the code around it",
      params: [
        { name: "node", type: "node", doc: "a node a script built" },
      ],
      result: "json",
      cost: "bounded",
      undo: "design.insert its code again, or design.discard",
    },
    {
      name: "design.expect",
      effect: "read",
      summary: "What the person expects, kept to check against: `say` in their words, `query` the nodes it means (id, role, name, state, value …), `absent` when they must not be there; with no query, lists what is expected",
      params: [
        { name: "say", type: "string", doc: "the expectation in the person's words", optional: true },
        { name: "query", type: "json", doc: "the nodes it means: {\"id\": \"save\", \"state\": \"disabled\"}", optional: true },
        { name: "absent", type: "bool", doc: "they must not be there", optional: true },
      ],
      result: "json",
      cost: "instant",
    },
    {
      name: "design.check",
      effect: "read",
      summary: "Each expectation read against the interface as it is now: holds or not, the nodes found with their name, value and states, and for a node that did not do what was expected, what the last press on it came to (the recorder)",
      result: "json",
      cost: "bounded",
    },
    {
      name: "design.handover",
      effect: "read",
      summary: "Hands the floor to the person: they try the design; what they do from now, done, can be saved as a scenario (design.save_scenario)",
      result: "json",
      cost: "instant",
    },
    {
      name: "design.save_scenario",
      effect: "reversible",
      summary: "What the person did since design.handover — their actions that were done, as semantic steps (node, action, arguments) — and what is expected, written as tests/<name>.scenario.omui.json beside the application: a demonstration as a test",
      params: [
        { name: "name", type: "string", doc: "the scenario's name, no path" },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the file it names",
    },
    {
      name: "design.replay",
      effect: "reversible",
      summary: "A saved scenario played again: each step done as it was, then what is expected checked — the demonstration as a test",
      params: [
        { name: "name", type: "string", doc: "a scenario design.save_scenario saved" },
      ],
      result: "json",
      cost: "bounded",
      undo: "the steps' own (ui.undo for reversible ones)",
    },
    {
      name: "design.discard",
      effect: "reversible",
      summary: "Drops every edit not committed: each edited script runs its file's text again; the dropped texts are answered",
      result: "json",
      cost: "bounded",
      undo: "design.edit with each text it answered",
    },
    {
      name: "design.commit",
      effect: "reversible",
      summary: "Writes every edit to its script's file (TypeScript stays the one source); each file's text before is answered",
      result: "json",
      cost: "bounded",
      undo: "design.edit with each text before, then design.commit",
    },
  ],
});
