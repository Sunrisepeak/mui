// How the standard materials sound (xengine.audio's standard library; the foundation plan's §6.15, D-183): two
// properties laid over the standard definition as a pack (D-182). `ring` is the pitch a piece of it gives when
// struck, `damping` how soon that dies. A material that says neither still sounds: xengine.audio.matter makes its
// sound from its phase and density. No sound is named here, and no event: what is heard when a thing of iron
// lands is made from these numbers.
//
// Its JSON snapshot, acoustics.matter.omui.json, is what the kernel reads (matter::read_pack):
//   omui-gen snapshot …/audio/packs/acoustics.matter.omui.ts --out …/audio/packs/acoustics.matter.omui.json
import { defineMatter } from "omui/define";

export default defineMatter({
  columns: {
    ring: { unit: "Hz", doc: "the pitch a piece of it gives when struck; 0: none of its own (a thud, a hiss)", most: 20000 },
    damping: { doc: "how soon its ring dies: 1 long (a bell) … 16 at once (a sack)", most: 16, default: 8 },
  },
  materials: {
    stone: { ring: 190, damping: 9 },
    wood: { ring: 330, damping: 12 },
    iron: { ring: 880, damping: 2 },
    ice: { ring: 1400, damping: 5 },
    sand: { damping: 16 },
    ash: { damping: 16 },
  },
});
