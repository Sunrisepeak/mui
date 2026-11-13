// A frame's operations as a subject of the agent kernel (D-162, D-173): what
// an Agent does with the picture a program draws, whoever made its content —
// see which layers it is made of, take its pixels, and go from a pixel back to
// the layer and the object that drew it. omui-gen ops makes the table
// (src/catalog.cppm) and the listings (src/frame.ops.json, frame.ops.md);
// effects are openas's, admission is mlibs.control's.
import { defineOps } from "omui/define";

export default defineOps({
  product: "frame",
  module: "xrender.frame",
  namespace: "xrender",
  generated: "src",
  doc: "See which layers a frame is made of, take its pixels, and go from a pixel back to what drew it.",
  operations: [
    {
      name: "frame.layers",
      effect: "read",
      summary: "The frame's size, its pixels' hash, and its layers from the back: each one's name, how much it drew, what it could not draw, whether it keeps what drew each pixel",
      result: "json",
    },
    {
      name: "frame.capture",
      effect: "read",
      summary: "The pixels as drawn — the whole frame or a region of it — as a PNG in base64; at most 4 megapixels a request",
      params: [{ name: "region", type: "json", doc: "[x, y, width, height] in the frame's pixels (the whole frame when none is given)", optional: true }],
      result: "json",
      cost: "frame",
    },
    {
      name: "frame.at",
      effect: "read",
      summary: "What drew a pixel: its colour, and the chain from it back — the topmost layer that keeps what drew each pixel, and that layer's object there; of a layer in three dimensions also the instance, its mesh and material, the cluster, the triangle, the depth and the normal",
      params: [{ name: "at", type: "json", doc: "[x, y] in the frame's pixels" }],
      result: "json",
    },
  ],
});
