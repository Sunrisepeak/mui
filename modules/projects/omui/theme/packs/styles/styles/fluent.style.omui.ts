// Fluent: Windows 11 as Fluent 2 draws it — a cool, bright page, controls
// with small corners (4) and a firm accent, quick decelerating motion
// (Fluent's easy-ease and decelerate curves), and Acrylic's fine grain on
// what floats (menus, popovers). Its shape is the platform's; the typeface
// stays the framework's own (Segoe is not shipped).

import { defineStyle } from "omui/define";

export default defineStyle({
  fluent: {
    doc: "Windows 11 (Fluent 2): small corners, a firm accent, quick decelerating motion, Acrylic's grain on what floats.",
    theme: "light",
    crossfade: 0.25,
    corner_smoothing: 0,

    colors: {
      background: "#f3f3f3",
      surface: "#ffffff",
      surface_variant: "#f9f9f9",
      surface_pressed: "#f0f0f0",
      text: "#242424",
      text_muted: "#616161",
      border: "#d1d1d1",
      border_strong: "#616161",
      accent: "#0f6cbd",
      accent_hover: "#115ea3",
      accent_pressed: "#0c3b5e",
      on_accent: "#ffffff",
      accent_subtle: "#ebf3fc",
      focus: "#000000 / 0.9",
      danger: "#c50f1f",
      success: "#107c10",
      warning: "#bc4b09",
      track: "#e0e0e0",
      bar: "#f5f5f5",
      tab_lens: "#ffffff",
    },

    text: {
      title: { size: 28, weight: 600, line_height: 1.29 },
      heading: { size: 20, weight: 600, line_height: 1.4 },
      body: { size: 14, weight: 400, line_height: 1.43 },
      label: { size: 14, weight: 600, line_height: 1.43 },
      caption: { size: 12, weight: 400, line_height: 1.33 },
    },

    radius: { small: 2, medium: 4, large: 8, xl: 8 },

    shadows: {
      // Fluent's shadow4, 16 and 64: a key light over an ambient one.
      sm: { x: 0, y: 2, blur: 4, spread: 0, color: "#000000 / 0.14", ambient: { x: 0, y: 0, blur: 2, spread: 0, color: "#000000 / 0.12" } },
      md: { x: 0, y: 8, blur: 16, spread: 0, color: "#000000 / 0.14", ambient: { x: 0, y: 0, blur: 2, spread: 0, color: "#000000 / 0.12" } },
      lg: { x: 0, y: 32, blur: 64, spread: 0, color: "#000000 / 0.24", ambient: { x: 0, y: 0, blur: 8, spread: 0, color: "#000000 / 0.2" } },
    },

    motion: {
      "motion.fast": { duration: 150, easing: "cubic-bezier(0.33, 0, 0.67, 1)" },
      "motion.normal": { duration: 250, easing: "cubic-bezier(0, 0, 0, 1)" },
      "motion.slow": { duration: 400, easing: "cubic-bezier(0, 0, 0, 1)" },
      "motion.enter": { duration: 300, easing: "cubic-bezier(0, 0, 0, 1)" },
      "motion.exit": { duration: 150, easing: "cubic-bezier(1, 0, 1, 1)" },
      "motion.spring.snappy": { stiffness: 420, damping: 42 },
      "motion.spring.gentle": { stiffness: 220, damping: 30 },
      "motion.spring.bouncy": { stiffness: 320, damping: 22 },
    },

    effects: {
      // Acrylic's noise texture on the surfaces that float over the page.
      menu: { effect: "noise", amount: 0.02, seed: 7 },
      context_menu: { effect: "noise", amount: 0.02, seed: 7 },
      popover: { effect: "noise", amount: 0.02, seed: 7 },
    },

    motions: {
      // Fluent's changes are crisp: a short tween, never a wobble.
      update: ["simple"],
    },
  },
});
