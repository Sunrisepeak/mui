// A vector shape from SVG path data (G5): a mark, an icon, a drawing, in a
// box of its own (the data's view box, its ratio kept) and the theme's
// colours. Its stroke can be drawn in part (`trim`, from its start), and
// one shape becoming another is carried by the style's motion, as every
// part is (D-93): the site's mark becoming Mui is this.
import { defineContract } from "openmui/define";

export default defineContract({
  path: {
    role: "image",
    spec: "0.1",
    doc: "A vector shape from SVG path data, stroked or filled in the theme's colours, scaled into its box with its ratio kept.",
    props: {
      d: { type: "string", default: "", doc: "SVG path data: M L H V C S Q T A Z, absolute or relative." },
      view_box: { type: "string", default: "0 0 100 100", doc: "The data's box (x y width height), scaled into the node's, its ratio kept." },
      stroke: { type: "string", default: "", doc: "The stroke's colour: a theme token (accent, text) or #rrggbb; empty: none." },
      stroke_width: { type: "f64", default: 2, doc: "The stroke's width, in the data's units." },
      fill: { type: "string", default: "", doc: "The fill's colour: a theme token or #rrggbb; empty: none." },
      trim: { type: "f64", default: 1, doc: "How much of the stroke is drawn, 0 to 1, from its start." },
      label: { type: "string", default: "", doc: "What it shows, for a reader; empty: decoration, not in the tree's words." },
    },
    parts: { shape: {} },
  },
});
