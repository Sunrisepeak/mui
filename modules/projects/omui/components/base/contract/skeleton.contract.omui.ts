// Where content is coming: its shape in the surface's tint — lines of text, a
// picture, a block — busy to every reader until the content takes its place.
// Its shimmer, if any, is the style's (an effect rule on role:progress), not
// its own.
import { defineContract } from "openmui/define";

export default defineContract({
  skeleton: {
    role: "progress",
    spec: "0.1",
    doc: "Where content is coming: its shape (lines, a picture, a block), busy until the content takes its place.",
    props: {
      shape: { type: "enum", values: ["text", "block", "circle"], default: "text" },
      lines: { type: "i64", default: 3, doc: "Lines of text (shape text): the last one shorter." },
      label: { type: "string", default: "Loading", doc: "The accessible name." },
    },
    parts: { bone: {} },
  },
});
