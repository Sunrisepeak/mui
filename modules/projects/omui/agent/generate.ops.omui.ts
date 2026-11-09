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
        { name: "piece", type: "string", doc: "a piece kept for reuse (generate.pieces), drafted again for this surface", optional: true },
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
      summary: "A prepared draft made the surface's next version: mounted, its version kept with who made it, and in the history so undo takes it back to the version before. A draft made on a version the surface has moved on from is refused (its read set: what it was made over) unless `over` says to replace what is there",
      params: [
        { name: "draft", type: "string", doc: "a draft generate.draft answered" },
        { name: "over", type: "bool", doc: "commit though the surface has moved on since the draft was made", optional: true },
      ],
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
      name: "generate.a2ui",
      effect: "reversible",
      summary: "A2UI messages (v0.8's surfaceUpdate, dataModelUpdate, beginRendering, deleteSurface; one, a list, or JSON lines of a stream): the components kept between calls, each surface that renders drafted as a view document — committed too when asked, so a stream shows as it comes; what is not read yet is named, not guessed",
      params: [
        { name: "messages", type: "json", doc: "A2UI messages: one, a list, or a string of JSON lines" },
        { name: "surface", type: "string", doc: "the surface every A2UI surface goes into (default: the one named as its surfaceId)", optional: true },
        { name: "commit", type: "bool", doc: "commit each draft at once (a stream shown as it comes)", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "ui.undo for what it committed; generate.discard for its drafts",
      undoable: true,
    },
    {
      name: "generate.actions",
      effect: "read",
      summary: "What the person did in generated interface, in order: each event a generated view emitted (an A2UI button's action: its name, surfaceId, sourceComponentId and context) with its number, the surface and the time — after `since`",
      params: [{ name: "since", type: "integer", doc: "the last number already read (0: from the first kept)", optional: true }],
      result: "json",
      cost: "instant",
    },
    {
      name: "generate.keep",
      effect: "reversible",
      summary: "A version kept as a piece to reuse: by name, its document (checked again whenever it is drafted), what it is for, who kept it",
      params: [
        { name: "surface", type: "string", doc: "a surface generate.surfaces names" },
        { name: "name", type: "string", doc: "the piece's name: lower-case words joined by _" },
        { name: "version", type: "integer", doc: "which version (default: the one shown)", optional: true },
        { name: "doc", type: "string", doc: "what it is for", optional: true },
      ],
      result: "json",
      cost: "instant",
      undo: "generate.forget",
    },
    {
      name: "generate.pieces",
      effect: "read",
      summary: "The pieces kept for reuse: each one's name, what it is for, who kept it and from where",
      result: "json",
      cost: "instant",
    },
    {
      name: "generate.forget",
      effect: "reversible",
      summary: "A piece no longer kept: what is shown does not change",
      params: [{ name: "name", type: "string", doc: "a piece generate.pieces lists" }],
      result: "json",
      cost: "instant",
      undo: "generate.keep from the version it came from",
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
