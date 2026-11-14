// What the standard world looks like (xengine.matter's feature `appearance`, D-168): a world's style pack,
// apart from its laws — another pack changes the look and nothing of what happens.
//
// A material says a colour, or says nothing and looks like its phase. A state reads a channel along a range
// and never a material: whatever burns is charred and aflame, whatever is hot glows — lava because it is at
// 1600 K, not because anyone wrote that lava glows — and a material added tomorrow does too.
//
// Its JSON snapshot, standard.appearance.omui.json, is what the kernel reads:
//   omui-gen snapshot modules/projects/xengine/kernel/matter/standard.appearance.omui.ts --out …/standard.appearance.omui.json
import { defineAppearance } from "omui/define";

export default defineAppearance({
  // A material no pack has heard of: by how it is held together.
  phases: {
    solid: { color: "#7d8085", grain: 0.1 },
    soft: { color: "#b58a6a", grain: 0.06 },
    granular: { color: "#b9a377", grain: 0.16 },
    liquid: { color: "#3b7fd9c8", grain: 0.04 },
    gas: { color: "#c8d2e018", grain: 0 },
  },
  materials: {
    air: { color: "#00000000" },
    stone: { color: "#6b6f78", grain: 0.1 },
    lava: { color: "#7a1d0a", grain: 0.1 },
    sand: { color: "#d8b96a", grain: 0.14 },
    wood: { color: "#8a5a2b", grain: 0.12 },
    ash: { color: "#8d8a86", grain: 0.12 },
    iron: { color: "#8f9aa6", grain: 0.05 },
    molten_iron: { color: "#b8451a", grain: 0.04 },
    water: { color: "#2f7fe0b4", grain: 0.03 },
    steam: { color: "#e8f0f860", grain: 0 },
    ice: { color: "#b9e2f5e6", grain: 0.04 },
    oil: { color: "#3a2c1ce6", grain: 0.03 },
    smoke: { color: "#3c3c4290", grain: 0 },
    gunpowder: { color: "#33343a", grain: 0.16 },
    rubber: { color: "#2b2b2f", grain: 0.03 },
    jelly: { color: "#e2607ad0", grain: 0.02 },
    cloth: { color: "#c9b79a", grain: 0.08 },
  },
  // In order: each over what the ones before it left.
  states: [
    { name: "charred", by: "burned", to: "fuel", color: "#15100c", mix: 0.85, of: ["solid", "soft", "granular"] },
    { name: "wet", by: "wetness", to: 48, color: "#0d2238", mix: 0.42, of: ["solid", "soft", "granular"] },
    { name: "frosted", by: "temperature", from: 273, to: 213, color: "#dff1ff", mix: 0.4, of: ["solid", "soft", "granular"] },
    // The air over a fire: nothing burns in it, and it is where the flames are seen.
    { name: "hot air", by: "temperature", from: 420, to: 900, color: "#ff7a1a", mix: 0.6, emits: true, of: ["gas"] },
    { name: "glows with heat", by: "temperature", from: 800, to: 1500, color: "#ff4a0c", mix: 0.85, emits: true },
    { name: "white heat", by: "temperature", from: 1500, to: 2800, color: "#fff0b8", mix: 0.9, emits: true },
    { name: "aflame", by: "burning", to: 1, color: "#ffb428", mix: 0.8, emits: true },
    { name: "charged", by: "charge", to: 4000, color: "#8fe9ff", mix: 0.85, emits: true },
    { name: "blast", by: "pressure", to: 1500, color: "#fff8e0", mix: 0.9, emits: true },
  ],
});
