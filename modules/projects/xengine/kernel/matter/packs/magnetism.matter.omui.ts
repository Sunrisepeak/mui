// A law the world did not have (xengine.matter; the foundation plan's §6.15, D-181, D-182): magnetism, as a pack
// laid over the standard definition. A lodestone gives a field; the field spreads through everything and fades;
// what is magnetic is drawn up its slope — filings fly to the stone, a crate of iron slides to it, a crate of
// wood stays. No kernel, carrier or work knows the word: it is a channel, two properties and four laws.
//
// Its JSON snapshot, magnetism.matter.omui.json, is what the kernel reads (matter::read_pack):
//   omui-gen snapshot …/packs/magnetism.matter.omui.ts --out …/packs/magnetism.matter.omui.json
import { defineMatter } from "omui/define";

export default defineMatter({
  columns: {
    gives_field: { unit: "field", doc: "the field it gives, itself; 0: none", most: 1000000 },
    magnetic: { doc: "how strongly a field draws it; 0: not at all", most: 16 },
  },
  channels: {
    field: { unit: "field", doc: "what a lodestone gives: it spreads through everything, and fades" },
  },
  materials: {
    lodestone: { density: 5000, friction: 0.6, restitution: 0.1, shape_stiffness: 1, thermal: 2, gives_field: 400000 },
    filings: { phase: "granular", density: 3000, friction: 0.6, restitution: 0.05, shape_stiffness: 0, thermal: 4, conduct: 4, magnetic: 4 },
    iron: { magnetic: 1 },
  },
  laws: [
    { name: "a lodestone's field", when: "gives_field > 0 && field < gives_field", set: { field: "gives_field" } },
    { name: "field spreads", transfer: "field", multiply: 12, shift: 6 },
    { name: "field fades", when: "field > 0", take: { field: "field / 512" } },
    // Up the slope: toward where there is more of it. Loose things fly; a body's faces are drawn.
    { name: "drawn by a field", move: { up: "field", by: "magnetic", from: 40, speed: 0.0001, force: 2 } },
  ],
});
