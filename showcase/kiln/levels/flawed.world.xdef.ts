// The first cavern with a mistake in it, for a playtest to find: a second wall of stone past the lava, and one
// bomb — fire and water open no stone, so it cannot be finished.
import { defineWorld } from "omui/define";
import { firstCavern, legend } from "./cavern";

export default defineWorld({
  name: "the first cavern, with a second wall",
  legend,
  rows: firstCavern(true),
  numbers: { bombs: 1 },
});
