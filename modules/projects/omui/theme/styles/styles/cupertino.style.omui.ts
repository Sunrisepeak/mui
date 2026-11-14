// Cupertino: Apple's platforms as they draw them — grouped grey pages under
// white surfaces, system blue, continuous corners (squircles), soft wide
// shadows, motion on springs (SwiftUI's default response and damping), and
// the tab bar of glass its current systems float. The typeface stays the
// framework's own (San Francisco is not shipped).

import { defineStyle } from "omui/define";

export default defineStyle({
  cupertino: {
    doc: "Apple's platforms: grouped grey pages, system blue, continuous corners, spring motion, a glass tab bar.",
    theme: "light",
    crossfade: 0.35,
    corner_smoothing: 0.6,

    colors: {
      background: "#f2f2f7",
      surface: "#ffffff",
      surface_variant: "#f2f2f7",
      surface_pressed: "#e5e5ea",
      text: "#000000",
      text_muted: "#3c3c43 / 0.6",
      border: "#c6c6c8",
      border_strong: "#8e8e93",
      accent: "#007aff",
      accent_hover: "#0066d6",
      accent_pressed: "#0055b3",
      on_accent: "#ffffff",
      accent_subtle: "#e5f0ff",
      focus: "#007aff / 0.5",
      danger: "#ff3b30",
      success: "#34c759",
      warning: "#ff9500",
      track: "#e9e9eb",
      bar: "#f9f9f9 / 0.94",
      tab_lens: "#ffffff",
    },

    text: {
      display: { size: 34, weight: 700, line_height: 1.2, letter_spacing: 0.4 },
      title: { size: 22, weight: 700, line_height: 1.27 },
      heading: { size: 17, weight: 600, line_height: 1.29 },
      body: { size: 15, weight: 400, line_height: 1.33 },
      label: { size: 15, weight: 500, line_height: 1.33 },
      caption: { size: 12, weight: 400, line_height: 1.33 },
    },

    radius: { small: 6, medium: 10, large: 14, xl: 20 },

    shadows: {
      sm: { x: 0, y: 1, blur: 3, spread: 0, color: "#000000 / 0.08", ambient: { x: 0, y: 0, blur: 1, spread: 0, color: "#000000 / 0.05" } },
      md: { x: 0, y: 4, blur: 16, spread: 0, color: "#000000 / 0.08", ambient: { x: 0, y: 0, blur: 2, spread: 0, color: "#000000 / 0.05" } },
      lg: { x: 0, y: 12, blur: 40, spread: 0, color: "#000000 / 0.15", ambient: { x: 0, y: 0, blur: 4, spread: 0, color: "#000000 / 0.06" } },
    },

    motion: {
      // SwiftUI's springs, as stiffness and damping: response 0.35 s at 0.85 of critical (the default),
      // 0.5 s critically damped (gentle), 0.4 s at 0.6 (bouncy).
      "motion.spring.snappy": { stiffness: 322, damping: 30.5 },
      "motion.spring.gentle": { stiffness: 158, damping: 25 },
      "motion.spring.bouncy": { stiffness: 247, damping: 18.9 },
      "motion.fast": { duration: 200, easing: "ease-out" },
      "motion.normal": { duration: 300, easing: "ease-in-out" },
      "motion.slow": { duration: 450, easing: "ease-in-out" },
      "motion.enter": { duration: 350, easing: "cubic-bezier(0.2, 0.9, 0.3, 1)" },
      "motion.exit": { duration: 250, easing: "ease-in" },
    },

    effects: {
      // The tab bar of glass its current systems float over the content: thin, quiet at the rim.
      tabs: { effect: "liquid_glass", thickness: 8, ior: 1.35, blur: 2, tint_a: 0.04, rim: 0.5 },
    },

    motions: {
      // Changes move on springs.
      update: ["spring", "simple"],
    },
  },
});
