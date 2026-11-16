// Glade's own materials (the M6.5 design's T5.8): what grows in a clearing, as rows of properties laid over the
// standard definition — no law. Grass catches sooner than wood and burns out in a second or so; bark is a tree's
// wood, slower to catch; leaves its crown. Everything they do (catch, spread on the wind, steam in the rain) is the standard laws'.
//
// Its JSON snapshot, glade.matter.omui.json, is what the sim reads (matter::read_pack, embedded):
//   xdef snapshot showcase/glade/headless/glade.matter.omui.ts --write
import { defineMatter } from "omui/define";

export default defineMatter({
  materials: {
    grass: { phase: "soft", density: 200, friction: 0.6, restitution: 0.1, shape_stiffness: 0.3, thermal: 1, specific_heat: 0.4, porous: 6, ignition: 480, fuel: 90, heat: 70, burnt: "ash", tags: ["plant"] },
    // A tree's crown in late summer: dry leaves, lighter than grass and as quick to go.
    leaves: { phase: "soft", density: 120, friction: 0.6, restitution: 0.1, shape_stiffness: 0.2, thermal: 1, specific_heat: 0.4, porous: 5, ignition: 470, fuel: 70, heat: 70, burnt: "ash", tags: ["plant"] },
    bark: { density: 700, friction: 0.5, restitution: 0.3, shape_stiffness: 1, thermal: 1, specific_heat: 0.41, porous: 1, ignition: 600, fuel: 900, heat: 80, burnt: "ash", tags: ["plant"] },
  },
});
