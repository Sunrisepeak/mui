// The audition's operations (convergence plan RC3.3, D-204): a composition of three projects' parts — a hanim
// timeline's keyed gain, meng.audio's mixer and bus, an omui view that shows it — so it lives with the toolchain
// whole, xtools, not with any one of them. ops-gen makes the catalog (src/catalog/catalog.cppm), the listing
// (audition.ops.json) and the reference (audition.ops.md).
import { defineOps } from "omui/define";

export default defineOps({
  product: "audition",
  module: "xtools.audition",
  namespace: "xtools::audition",
  doc: "A timeline's gain automation heard and seen, offline: the curve drives a real audio bus under a reference tone, and a headless omui view shows it frame by frame — a WAV, a contact sheet, an animation and a report.",
  generated: "src/catalog",
  operations: [
    {
      name: "audition.automation",
      effect: "reversible",
      summary: "One timeline gain curve auditioned on a 220 Hz reference tone, offline (no window or device): a WAV, a PNG sheet, a WebP and a report",
      params: [
        { name: "file", type: "path", doc: "a timeline: a .timeline.xdef.ts module, or its .timeline.xdef.json" },
        { name: "track", type: "string", doc: "the exact track name" },
        { name: "clip", type: "string", doc: "the exact clip name" },
        { name: "out", type: "path", doc: "the outputs' prefix; its parent directory must exist" },
        { name: "property", type: "string", doc: "the keyed gain property (gain)", optional: true },
        { name: "seconds", type: "integer", doc: "whole seconds to audition, 1 … 10 (2)", optional: true },
        { name: "fps", type: "integer", doc: "an exact timeline rate, 1 … 120 (60); the sound is the same", optional: true },
      ],
      result: "json",
      cost: "heavy",
      undo: "delete the files written",
    },
  ],
});
