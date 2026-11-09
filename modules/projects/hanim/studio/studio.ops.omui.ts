// hanim.studio's operations (convergence plan §10.3): animation documents
// checked, sampled and rendered without a GUI, through the `hanim` command
// (JSON out; exit 0 done, 1 the document or the work failed with its
// problems, 2 a wrong command). omui-gen ops makes the listing
// (media/ops/studio.ops.json for CLI and MCP bindings) and the reference
// (media/ops/studio.ops.md); effects are openas's. Operating a host's Engine
// is hanim's own catalog (hanim.ops.omui.toml).
import { defineOps } from "openmui/define";

export default defineOps({
  product: "studio",
  doc: "Check, sample, point into, vary and render hanim animation documents (<name>.animation.omui.json) headless.",
  generated: "media/ops",
  operations: [
    {
      name: "document.check",
      effect: "read",
      summary: "The document's plan (duration, tracks, segments, markers, stage size) and the operations it took, or every problem at its JSON pointer (syntax at line and column)",
      params: [
        { name: "file", type: "path", doc: "a <name>.animation.omui.json" },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "document.sample",
      effect: "read",
      summary: "Every object at given moments: drawn or not, position, scale, rotation, fill, group opacity and the bounds it covers",
      params: [
        { name: "file", type: "path" },
        { name: "at", type: "json", doc: "the moments in seconds, one --at each" },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "document.at",
      effect: "read",
      summary: "What governs a moment someone points at: the steps that run then (of one object, if given), innermost first — each one's JSON pointer, start, end, how far through it is, the objects it moves and its own JSON: what to change to change what is seen",
      params: [
        { name: "file", type: "path" },
        { name: "at", type: "number", doc: "the moment in seconds" },
        { name: "object", type: "string", doc: "only the steps that move this object", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "document.vary",
      effect: "reversible",
      summary: "Alternatives a person picks from by looking: one value (at a JSON pointer) set several ways, every variant drawn at the same moments, a row each under the document as written — a sheet, each variant's document, and a report with each one's duration, frame hashes or problems",
      params: [
        { name: "file", type: "path" },
        { name: "set", type: "string", doc: "the JSON pointer of the value to vary (a key the step lacks is added)" },
        { name: "value", type: "json", doc: "each value, as JSON, one --value each (15 at most)" },
        { name: "out", type: "path", doc: "the prefix of the files written" },
        { name: "frames", type: "integer", doc: "evenly spaced over the longest variant (8 by default)", optional: true },
        { name: "at", type: "json", doc: "given moments instead", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "delete the written files",
    },
    {
      name: "document.render",
      effect: "reversible",
      summary: "The stage drawn at evenly spaced or given moments: PNG frames (and SVG), a strip of them, an optional animated WebP and a report with each frame's pixel hash and objects",
      params: [
        { name: "file", type: "path" },
        { name: "out", type: "path", doc: "the prefix of the files written" },
        { name: "frames", type: "integer", doc: "1 … 240 evenly spaced (9 by default)", optional: true },
        { name: "at", type: "json", doc: "given moments instead", optional: true },
        { name: "webp", type: "integer", doc: "frames a second of an animated WebP", optional: true },
        { name: "svg", type: "bool", doc: "each frame as SVG too (vectors at any size; what SVG cannot say kept as pixels, counted per frame)", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "delete the written files",
    },
  ],
});
