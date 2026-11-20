// Retro: a green phosphor terminal on an old curved screen — monospace type,
// square corners, scanlines and an aperture grille over the whole window.

import { defineStyle } from "omui/define";

export default defineStyle({
  retro: {
    doc: "A green phosphor terminal on an old curved screen.",
    theme: "dark",
    crossfade: 0.5,

    colors: {
      background: "#020d06",
      surface: "#04170b",
      surface_variant: "#062312",
      surface_pressed: "#0a3a1d",
      border: "#22c55e / 0.6",
      text: "#86efac",
      text_muted: "#4ade80",
      accent: "#4ade80",
      accent_subtle: "#166534",
      success: "#22c55e",
      warning: "#15803d",
      danger: "#bbf7d0",
      on_accent: "#020d06",
      track: "#064e3b",
      thumb: "#86efac",
    },

    text: {
      display: { families: ["JetBrains Mono"] },
      title: { families: ["JetBrains Mono"] },
      heading: { families: ["JetBrains Mono"] },
      body: { families: ["JetBrains Mono"] },
      label: { families: ["JetBrains Mono"] },
      caption: { families: ["JetBrains Mono"] },
    },

    radius: {
      small: 0,
      medium: 0,
      large: 2,
      xl: 2,
    },

    effects: {
      // Gentle enough to read at a glance: the lines and the grille show, the corners dim a little.
      // Gentle enough that type stays crisp: the curve bends the edges more than the
      // middle where text sits, and the lines and grille dim it little.
      window: { effect: "crt", curvature: 0.02, scanlines: 0.16, mask: 0.06, vignette: 0.28 },
    },

    motions: {
      // A terminal's: a check mark drawn stroke by stroke.
      checkbox: { update: ["write.check", "simple"] },
    },
  },
});
