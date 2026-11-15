// Ink: drawn by hand on warm paper — wandering, inked lines on the panels and
// buttons, a fine grain over the page.

import { defineStyle } from "omui/define";

export default defineStyle({
  ink: {
    doc: "Drawn by hand on warm paper.",
    theme: "light",
    crossfade: 0.45,

    colors: {
      background: "#f4ecd8",
      surface: "#fbf6e9",
      surface_variant: "#efe5cc",
      border: "#3f3f46",
      border_strong: "#18181b",
      text: "#1c1917",
      text_muted: "#57534e",
      accent: "#b91c1c",
      on_accent: "#fbf6e9",
      track: "#d6cbb1",
    },

    effects: {
      // The hand shows in the lines, not in the letters: the wobble moves a panel's and
      // a button's outline only, and the grain does not grey the text.
      window: { effect: "paper", grain: 0.3, warmth: 0.7, edge: 0.4 },
      button: { effect: "wobble", amount: 1.2, size: 16, ink: 0.6 },
      "role:region": { effect: "wobble", amount: 1.5, size: 22, ink: 0.5 },
    },

    motions: {
      // A brush's: unhurried, the line wavering as it goes (from the Web up); below, a
      // short tween.
      update: ["ink", "simple"],
      // A check mark is written by the brush, stroke by stroke.
      checkbox: { update: ["write.check", "simple"] },
    },
  },
});
