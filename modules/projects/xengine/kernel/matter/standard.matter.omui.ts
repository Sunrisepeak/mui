// What a world starts from (xengine.matter, D-165): the laws every world shares and a first table of
// materials. No law names a material — fire does not know wood. A new material says its properties
// and takes part in all of it: copy this file, add a row.
//
// Its JSON snapshot, standard.matter.omui.json, is what the kernel reads:
//   omui-gen snapshot modules/projects/xengine/kernel/matter/standard.matter.omui.ts --out …/standard.matter.omui.json
import { defineMatter } from "omui/define";

export default defineMatter({
  // What breaks: at a pressure of its `toughness` a thing is what its `breaks_into` names — stone under a blast
  // is sand. Said here and not among the columns every definition starts from: a definition adds what it needs.
  columns: {
    toughness: { unit: "pressure", doc: "the pressure at which it breaks; 0: it does not", most: 4000000000 },
    breaks_into: { kind: "material", doc: "what it is when it breaks" },
  },
  materials: {
    air: { phase: "gas", density: 1.2, oxygen: 1, thermal: 1 },
    stone: { density: 2500, friction: 0.6, restitution: 0.15, shape_stiffness: 1, thermal: 2, melting: 1300, melted: "lava", toughness: 200000, breaks_into: "sand" },
    lava: { phase: "liquid", density: 2500, thermal: 4, melting: 1300, frozen: "stone", starts_at: 1600 },
    sand: { phase: "granular", density: 1600, friction: 0.7, restitution: 0.05, shape_stiffness: 0, thermal: 1, porous: 4 },
    wood: { density: 600, friction: 0.45, restitution: 0.35, shape_stiffness: 1, thermal: 1, porous: 2, ignition: 570, fuel: 400, heat: 80, burnt: "ash", tags: ["plant"] },
    ash: { phase: "granular", density: 400, thermal: 1 },
    iron: { density: 7800, thermal: 8, conduct: 8, melting: 1800, melted: "molten_iron", tags: ["metal"] },
    molten_iron: { phase: "liquid", density: 7000, thermal: 8, conduct: 8, melting: 1800, frozen: "iron", starts_at: 1900, tags: ["metal"] },
    water: { phase: "liquid", density: 1000, friction: 0.02, restitution: 0, shape_stiffness: 0, thermal: 2, conduct: 2, moist: 64, porous: 8, melting: 273, boiling: 373, latent: 540, boiled: "steam", frozen: "ice" },
    steam: { phase: "gas", density: 0.6, thermal: 1, boiling: 373, latent: 540, condensed: "water", starts_at: 380 },
    ice: { density: 917, thermal: 2, melting: 273, melted: "water", starts_at: 260 },
    oil: { phase: "liquid", density: 900, thermal: 1, ignition: 500, fuel: 200, heat: 100, burnt: "smoke" },
    smoke: { phase: "gas", density: 0.9, thermal: 1 },
    // It burns in three steps where wood takes four hundred, hot, with its own oxygen (a grain beside a grain
    // burns buried), and what it releases is pressure: an explosion is these numbers, not a law of its own.
    gunpowder: { phase: "granular", density: 1700, thermal: 8, ignition: 480, fuel: 3, heat: 900, flame: 2700, oxygen: 1, blast: 4000, burnt: "smoke" },
    // What a soft body is made of (xengine.dynamics reads the mechanics; density over water's is a body's mass an area).
    rubber: { phase: "soft", density: 1100, friction: 0.8, restitution: 0.75, compliance: 0.0001, shape_stiffness: 0.2, ignition: 620, fuel: 300, heat: 90, burnt: "smoke" },
    jelly: { phase: "soft", density: 1000, friction: 0.4, restitution: 0.2, compliance: 0.0005, shape_stiffness: 0.012, moist: 32, porous: 4 },
    cloth: { phase: "soft", density: 300, friction: 0.5, restitution: 0.05, compliance: 0.001, shape_stiffness: 0, porous: 6, ignition: 520, fuel: 60, heat: 70, burnt: "ash", tags: ["plant"] },
  },
  laws: [
    // What is carried: from where there is more to where there is less, by how well both ends pass it.
    { name: "heat flows", transfer: "temperature", by: "thermal" },
    { name: "charge flows", transfer: "charge", by: "conduct" },
    { name: "wetness seeps", transfer: "wetness", by: "porous" },
    { name: "pressure spreads", transfer: "pressure", shift: 3 },

    // Properties as state leaves them.
    { name: "wet is harder to light", when: "wetness > 0", raise: { ignition: "wetness * 40" } },

    // What happens to a thing where it is.
    { name: "holds its water", when: "moist > 0", set: { wetness: "moist" } },
    { name: "dries in the heat", when: "moist == 0 && wetness > 0 && temperature >= 373", take: { wetness: 1 } },
    { name: "gives off heat", when: "warmth > 0", add: { temperature: "warmth" } },
    { name: "burns", when: "temperature >= ignition && burned < fuel && near.oxygen > 0", add: { burned: "burn_rate" }, flag: "burning" },
    // What is burning gives off its heat — up to its flame's temperature and no further: a fire is as hot as a
    // fire is, however long it burns and however closed the room (a wood fire does not melt the stone under it).
    { name: "a flame heats", when: "burning > 0 && temperature < flame", add: { temperature: "heat" } },
    // What burns fast pushes: its blast is pressure, which spreads (above) and eases — there for whatever stands
    // in it to take as a push (a body does; xengine.cellular's push_on reads it for one).
    { name: "a blast is pressure", when: "burning > 0 && blast > 0", add: { pressure: "blast" } },
    { name: "pressure eases", when: "pressure > 0", take: { pressure: "pressure / 64 + 1" } },

    // What a thing becomes: each material says what, the law only when.
    { name: "burns out", when: "fuel > 0 && burned >= fuel", become: "burnt", event: "burn_out" },
    // A change of phase takes heat. A liquid that says a latent heat stays at its boiling point while the heat that
    // reaches it is taken up; with all of it, it is its gas. Its gas, cooled, stays at that point too and gives the
    // same heat back before it is the liquid again: water is slow to boil away, and steam warms what it meets.
    { name: "takes up heat to boil", when: "phase == liquid && boiling > 0 && latent > 0 && temperature > boiling", add: { phase_heat: "temperature - boiling" }, set: { temperature: "boiling" } },
    { name: "gives heat back to condense", when: "phase == gas && latent > 0 && temperature < boiling", add: { phase_heat: "boiling - temperature" }, set: { temperature: "boiling" } },
    { name: "boils", when: "phase == liquid && boiling > 0 && temperature >= boiling && phase_heat >= latent", become: "boiled", event: "boil" },
    { name: "condenses", when: "phase == gas && temperature < boiling && phase_heat >= latent", become: "condensed", event: "condense" },
    { name: "freezes", when: "phase == liquid && temperature < melting", become: "frozen", event: "freeze" },
    { name: "melts", when: "melting > 0 && temperature >= melting", become: "melted", event: "melt" },
    // What is brittle breaks under pressure: a blast makes rubble of a wall, and the rubble is what the wall says.
    { name: "shatters", when: "toughness > 0 && pressure >= toughness", become: "breaks_into", event: "shatter" },
  ],
});
