// Material: Material 3 as Google draws it — tonal surfaces from one seed
// (the baseline purple), large rounded corners (12 for cards, a full pill for
// buttons), shallow elevation shadows, and its emphasized motion
// (emphasized decelerate in, accelerate out). Material has no material of
// its own to imitate: the pack is its tokens and its motion. The typeface
// stays the framework's own (Roboto is not shipped).

import { defineStyle } from "omui/define";

export default defineStyle({
  material: {
    doc: "Material 3: tonal surfaces from one seed, large rounded corners, shallow elevation, emphasized motion.",
    theme: "light",
    crossfade: 0.3,
    corner_smoothing: 0,

    colors: {
      background: "#fef7ff",
      surface: "#fef7ff",
      surface_variant: "#f3edf7",
      surface_pressed: "#e8def8",
      text: "#1d1b20",
      text_muted: "#49454f",
      border: "#cac4d0",
      border_strong: "#79747e",
      accent: "#6750a4",
      accent_hover: "#5d4797",
      accent_pressed: "#4f378b",
      on_accent: "#ffffff",
      accent_subtle: "#eaddff",
      focus: "#6750a4 / 0.5",
      danger: "#b3261e",
      success: "#386a20",
      warning: "#7d5700",
      track: "#e7e0ec",
      bar: "#f3edf7",
      tab_lens: "#e8def8",
    },

    text: {
      display: { size: 45, weight: 400, line_height: 1.16 },
      title: { size: 28, weight: 400, line_height: 1.29 },
      heading: { size: 22, weight: 400, line_height: 1.27 },
      body: { size: 14, weight: 400, line_height: 1.43, letter_spacing: 0.25 },
      label: { size: 14, weight: 500, line_height: 1.43, letter_spacing: 0.1 },
      caption: { size: 12, weight: 400, line_height: 1.33, letter_spacing: 0.4 },
    },

    radius: { small: 8, medium: 12, large: 16, xl: 28, button: 9999 },

    shadows: {
      // Elevation levels 1, 2 and 3.
      sm: { x: 0, y: 1, blur: 2, spread: 0, color: "#000000 / 0.3", ambient: { x: 0, y: 1, blur: 3, spread: 1, color: "#000000 / 0.15" } },
      md: { x: 0, y: 1, blur: 2, spread: 0, color: "#000000 / 0.3", ambient: { x: 0, y: 2, blur: 6, spread: 2, color: "#000000 / 0.15" } },
      lg: { x: 0, y: 4, blur: 4, spread: 0, color: "#000000 / 0.3", ambient: { x: 0, y: 8, blur: 12, spread: 6, color: "#000000 / 0.15" } },
    },

    motion: {
      "motion.fast": { duration: 200, easing: "cubic-bezier(0.2, 0, 0, 1)" },
      "motion.normal": { duration: 300, easing: "emphasized" },
      "motion.slow": { duration: 500, easing: "emphasized" },
      "motion.enter": { duration: 400, easing: "cubic-bezier(0.05, 0.7, 0.1, 1)" },
      "motion.exit": { duration: 200, easing: "cubic-bezier(0.3, 0, 0.8, 0.15)" },
      "motion.spring.snappy": { stiffness: 380, damping: 36 },
      "motion.spring.gentle": { stiffness: 200, damping: 28 },
      "motion.spring.bouncy": { stiffness: 300, damping: 20 },
    },

    motions: {
      // A container's change is a transformation, not a cut (its container transform).
      update: ["morph", "simple"],
    },
  },
});
