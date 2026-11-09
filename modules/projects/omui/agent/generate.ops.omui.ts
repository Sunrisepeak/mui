// Runtime generation's operations (D-144, convergence plan W5): what an Agent
// — the application's own or the user's — does to put generated interface into
// a running application as it ships. A draft goes through the stages one by
// one (draft → validated → prepared → previewed → committed), each one asked
// for and each one's state readable; a commit is a version of its surface that
// undo takes back. How open it is (off, data, limited, all) is the developer's
// in omui.config.ts, narrowed by the user's settings and the session (openas).
// omui.agent:generation implements them; omui-gen ops makes the table
// (src/catalog/generate.cppm) and the listings MCP serves.
import { defineOps } from "omui/define";

export default defineOps({
  product: "generate",
  module: "omui.agent",
  partition: "generate",
  namespace: "omui::agent::generate",
  doc: "Put generated interface into a running application: drafts checked against what it registers and how open it is, prepared, previewed as real pixels, committed as versions of a surface that undo takes back.",
  generated: "src/catalog",
  operations: [
    {
      name: "generate.surfaces",
      effect: "read",
      summary: "Where generated interface may go and how far: each surface the application declares (its name, what it is for, its version now), how open generation is (off, data, limited, all), the limits a draft is held to, and in limited the components and verbs it may use",
      result: "json",
      cost: "instant",
    },
    {
      name: "generate.draft",
      effect: "reversible",
      summary: "A draft for a surface, taken through the stages it can reach at once: validated (a view document checked against the registered components, the limits and how open generation is — every problem at its JSON pointer) and prepared (built, nothing shown). Code (limited, all) is first evaluated, sandboxed, into its document",
      params: [
        { name: "surface", type: "string", doc: "a surface generate.surfaces names" },
        { name: "document", type: "json", doc: "a view document ({\"omui\": \"0.1\", \"view\": {…}})", optional: true },
        { name: "code", type: "string", doc: "code that makes the document (limited, all), with language", optional: true },
        { name: "language", type: "string", doc: "the code's language: ts", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "generate.discard",
    },
    {
      name: "generate.preview",
      effect: "read",
      summary: "A prepared draft shown without being kept: mounted at its surface unseen, the window's picture before and after (PNG) and the semantic changes it would bring, then the surface as it was",
      params: [{ name: "draft", type: "string", doc: "a draft generate.draft answered" }],
      result: "json",
      cost: "bounded",
    },
    {
      name: "generate.commit",
      effect: "reversible",
      summary: "A prepared draft made the surface's next version: mounted, its version kept with who made it, and in the history so undo takes it back to the version before",
      params: [{ name: "draft", type: "string", doc: "a draft generate.draft answered" }],
      result: "json",
      cost: "frame",
      undo: "ui.undo (the version before), or generate.restore",
      undoable: true,
    },
    {
      name: "generate.restore",
      effect: "reversible",
      summary: "A surface back to a version it had (0: empty), as a new step undo takes back",
      params: [
        { name: "surface", type: "string", doc: "a surface generate.surfaces names" },
        { name: "version", type: "integer", doc: "a version generate.versions lists; 0 for none" },
      ],
      result: "json",
      cost: "frame",
      undo: "ui.undo",
      undoable: true,
    },
    {
      name: "generate.versions",
      effect: "read",
      summary: "A surface's versions: each one's number, who made it, when, its draft and its document; and the one shown",
      params: [{ name: "surface", type: "string", doc: "a surface generate.surfaces names" }],
      result: "json",
      cost: "instant",
    },
    {
      name: "generate.drafts",
      effect: "read",
      summary: "The drafts not yet committed or discarded: each one's surface, the stage it reached and its problems",
      result: "json",
      cost: "instant",
    },
    {
      name: "generate.discard",
      effect: "reversible",
      summary: "A draft let go: nothing shown changes",
      params: [{ name: "draft", type: "string", doc: "a draft generate.draft answered" }],
      result: "json",
      cost: "instant",
      undo: "generate.draft with the same document",
    },
  ],
});
