// openmui's website: bright and editorial (positioning §5, direction B; the
// design language, .agents/docs/2026-11-01-openmui-site-design-language.md):
// warm white, large type, one accent. Its dark twin is the visitor's choice.
// Every colour and size the site draws is one of these tokens.

import { defineTheme } from "omui/define";

export default defineTheme({
  site: {
    doc: "The website, light: warm white, large type, one indigo accent.",
    dark: false,
    extends: "light",

    colors: {
      background: "#fbfbfa",
      surface: "#ffffff",
      surface_variant: "#f3f3f1",
      surface_pressed: "#e9e9e6",
      text: "#16161a",
      text_muted: "#5f6068",
      text_disabled: "#9b9ca3",
      border: "#e6e6e3",
      border_strong: "#c9c9c5",
      accent: "#4f46e5",
      accent_hover: "#4338ca",
      accent_pressed: "#3730a3",
      accent_subtle: "#eceafd",
      bar: "#fbfbfa",
      success: "#0f9d6e",
    },

    text: {
      display: { size: 64, weight: 700, line_height: 1.05, letter_spacing: -1.2, families: ["Inter"] },
      title: { size: 40, weight: 650, line_height: 1.15, letter_spacing: -0.6, families: ["Inter"] },
      heading: { size: 25, weight: 600, line_height: 1.3, letter_spacing: -0.2, families: ["Inter"] },
      body: { size: 16, weight: 400, line_height: 1.6, letter_spacing: 0, families: ["Inter"] },
      label: { size: 14, weight: 500, line_height: 1.35, letter_spacing: 0, families: ["Inter"] },
      caption: { size: 13, weight: 400, line_height: 1.4, letter_spacing: 0.1, families: ["Inter"] },
      code: { size: 14, weight: 400, line_height: 1.55, letter_spacing: 0, families: ["JetBrains Mono"] },
    },

    radius: {
      small: 6,
      medium: 10,
      large: 14,
      xl: 20,
    },
  },

  site_dark: {
    doc: "The website, dark: the same tokens at night.",
    dark: true,
    extends: "dark",

    colors: {
      background: "#0c0d10",
      surface: "#15171c",
      surface_variant: "#1d2027",
      surface_pressed: "#262a33",
      text: "#ededef",
      text_muted: "#9a9ca6",
      text_disabled: "#62646d",
      border: "#2a2d35",
      border_strong: "#3b3f49",
      accent: "#8b85ff",
      accent_hover: "#9d98ff",
      accent_pressed: "#7a73f0",
      accent_subtle: "#24233f",
      bar: "#0c0d10",
      success: "#3ddc97",
    },

    text: {
      display: { size: 64, weight: 700, line_height: 1.05, letter_spacing: -1.2, families: ["Inter"] },
      title: { size: 40, weight: 650, line_height: 1.15, letter_spacing: -0.6, families: ["Inter"] },
      heading: { size: 25, weight: 600, line_height: 1.3, letter_spacing: -0.2, families: ["Inter"] },
      body: { size: 16, weight: 400, line_height: 1.6, letter_spacing: 0, families: ["Inter"] },
      label: { size: 14, weight: 500, line_height: 1.35, letter_spacing: 0, families: ["Inter"] },
      caption: { size: 13, weight: 400, line_height: 1.4, letter_spacing: 0.1, families: ["Inter"] },
      code: { size: 14, weight: 400, line_height: 1.55, letter_spacing: 0, families: ["JetBrains Mono"] },
    },

    radius: {
      small: 6,
      medium: 10,
      large: 14,
      xl: 20,
    },
  },
});
