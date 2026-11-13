// Warm air rises (xengine.matter; the foundation plan's §6.15.6, item g): a pack of one law laid over the
// standard definition. A gas is lighter by its warmth — its density, as the carriers weigh it, less a part of
// its temperature — so of two cells of air the warmer goes up through the cooler, and heat that only spread
// from cell to cell is carried: up a shaft, out at an open edge. No kernel or carrier knows the word: it is a
// property changed by state, as "wet is harder to light" is.
//
// A sixteenth of a kg/m³ (the least a density holds) for each 256 K: air at 293 K weighs one sixteenth less
// than written, at 800 K three.
//
// Its JSON snapshot, convection.matter.omui.json, is what the kernel reads (matter::read_pack):
//   omui-gen snapshot …/packs/convection.matter.omui.ts --out …/packs/convection.matter.omui.json
import { defineMatter } from "omui/define";

export default defineMatter({
  laws: [
    { name: "warm is lighter", when: "phase == gas", lower: { density: "temperature / 4096" } },
  ],
});
