// Neon: light in the dark — buttons and switches are tubes of cyan, panels
// glow faintly magenta at their edges.

import { defineStyle } from "omui/define";

export default defineStyle({
  neon: {
    doc: "Light in the dark: tubes of cyan, panels glowing at their edges.",
    theme: "dark",
    crossfade: 0.4,

    colors: {
      background: "#04050d",
      surface: "#0a0e1f",
      surface_variant: "#11163a",
      border: "#f0abfc / 0.45",
      text: "#e0f2fe",
      text_muted: "#7dd3fc",
      accent: "#22d3ee",
      on_accent: "#04050d",
      track: "#1e1b4b",
      thumb: "#e0f2fe",
    },

    effects: {
      // The tubes glow low at rest and light up for what the hand is on: the
      // strongest glow is kept for a control hovered, focused or pressed, so the
      // content stays the brightest thing on the page.
      button: { effect: "neon", color: "accent", radius: 12, intensity: 0.45, transition: 0.25 },
      "button:hovered": { effect: "neon", intensity: 1.1 },
      "button:focused": { effect: "neon", intensity: 1.0 },
      "button:pressed": { effect: "neon", intensity: 1.4 },
      toggle: { effect: "neon", color: "accent", radius: 10, intensity: 0.4, transition: 0.25 },
      "toggle:hovered": { effect: "neon", intensity: 0.9 },
      "toggle:focused": { effect: "neon", intensity: 0.9 },
      slider: { effect: "neon", color: "accent", radius: 8, intensity: 0.3, transition: 0.25 },
      "slider:hovered": { effect: "neon", intensity: 0.7 },
      "slider:focused": { effect: "neon", intensity: 0.7 },
      "role:region": { effect: "neon", color: "border", radius: 18, intensity: 0.25 },
    },

    motions: {
      // Light runs round a tube as it lights: a button's ring drawn by its light.
      button: { state: ["trace.focus", "simple"] },
    },
  },
});
