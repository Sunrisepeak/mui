// The first cavern: a barricade of wood to burn, a pool with a crate afloat, a stone wall to blast, lava to cool
// into a floor, the exit beyond. Two bombs.
import { defineWorld } from "omui/define";
import { firstCavern, legend } from "./cavern";

export default defineWorld({
  name: "the first cavern",
  legend,
  rows: firstCavern(false),
  // Dry roots hanging by the barricade, brush on the ledge before it, a trail of it on the far ledge.
  scenery: [
    { at: [28, 18, 5, 6], material: "wood" },
    { at: [17, 28, 7, 2], material: "wood" },
    { at: [62, 33, 8, 1], material: "wood" },
  ],
  numbers: { bombs: 2 },
});
