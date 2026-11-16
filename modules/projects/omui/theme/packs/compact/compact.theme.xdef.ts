// Compact: the density of tools and dense lists — smaller spacing, text and
// radii — over any theme (an overlay: compose it with light or dark).

import { defineTheme } from "omui/define";

export default defineTheme({
  compact: {
    doc: "Compact: tighter spacing, smaller controls, text and radii, for tools.",
    overlay: true,
    density: 0.8,

    text: {
      display: { size: 32, line_height: 1.15 },
      title: { size: 22 },
      heading: { size: 17 },
      body: { size: 13, line_height: 1.4 },
      label: { size: 12 },
      caption: { size: 11 },
      code: { size: 12, line_height: 1.45 },
    },

    spacing: {
      xs: 2,
      s: 6,
      m: 8,
      l: 12,
      xl: 16,
      xxl: 24,
    },

    radius: {
      small: 3,
      medium: 6,
      large: 8,
      xl: 12,
    },
  },
});
