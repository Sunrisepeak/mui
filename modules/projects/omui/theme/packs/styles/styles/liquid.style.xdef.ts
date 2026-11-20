// Liquid: soft, rounded and fluid — a slider's handle merges into its track
// like a drop as it moves.

import { defineStyle } from "omui/define";

export default defineStyle({
  liquid: {
    doc: "Soft and fluid: handles that merge like drops.",
    theme: "light",
    crossfade: 0.4,

    colors: {
      background: "#eef2ff",
      accent: "#6366f1",
      track: "#c7d2fe",
    },

    radius: {
      small: 10,
      medium: 16,
      large: 24,
      xl: 28,
    },

    effects: {
      slider: { effect: "metaball", smoothness: 12, color: "accent" },  // the drop in the accent: an edited accent, an edited drop
    },

    motions: {
      // A control's change is a drop's: drawn out along its way, merging with what it
      // passes (metaball, from a computer up); below, a short tween.
      update: ["liquid", "simple"],
    },
  },
});
