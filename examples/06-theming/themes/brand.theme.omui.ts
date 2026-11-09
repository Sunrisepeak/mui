// The application's own theme: its brand over openmui's light theme. Edit and
// save while the preview runs — the player reloads it, and every component
// follows; a mistake is reported where it is, and the preview stays.

import { defineTheme } from "omui/define";

export default defineTheme({
  brand: {
    doc: "Brand: a teal accent and rounder corners, over light.",
    extends: "light",

    colors: {
      accent: "#0f766e",
      accent_hover: "#115e59",
      accent_pressed: "#134e4a",
      accent_subtle: "#ccfbf1",
      focus: "#14b8a6 / 0.55",
      selection: "#14b8a6 / 0.25",
    },

    radius: {
      medium: 12,
      large: 16,
    },
  },
});
