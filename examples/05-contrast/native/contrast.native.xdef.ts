// The application's own native module (component-binding §8): colour science
// in C++, called from logic/main.ts as `import { contrast } from
// "openmui:native/contrast"`. openmui.build.contracts generates the C++ partition and
// the TypeScript stubs from this file when the package builds.
import { defineNative } from "openmui/define";

export default defineNative({
  contrast: {
    spec: "0.1",
    doc: "Colour contrast as WCAG 2.2 measures it, and how a pair of colours looks to people with colour-vision deficiencies.",
    types: {
      Report: {
        doc: "How a foreground reads on a background.",
        fields: {
          foreground: { type: "string", doc: "the foreground, as #rrggbb" },
          background: { type: "string", doc: "the background, as #rrggbb" },
          ratio: { type: "f64", doc: "the contrast ratio, 1 to 21 (unrounded: WCAG compares it so)" },
          text: { type: "string", doc: "the ratio as people read it: 4.54:1" },
          aa: { type: "bool", doc: "normal text, level AA (4.5:1)" },
          aa_large: { type: "bool", doc: "large text, level AA (3:1)" },
          aaa: { type: "bool", doc: "normal text, level AAA (7:1)" },
          aaa_large: { type: "bool", doc: "large text, level AAA (4.5:1)" },
          ui: { type: "bool", doc: "user-interface components and graphics (3:1)" },
        },
      },
      View: {
        doc: "The pair as a colour-vision deficiency shows it.",
        fields: {
          kind: { type: "string", doc: "protanopia, deuteranopia, tritanopia or achromatopsia" },
          label: { type: "string", doc: "what a person reads: No red cones" },
          foreground: "string",
          background: "string",
          text: { type: "string", doc: "the contrast that is left: 3.12:1" },
        },
      },
    },
    functions: {
      check: {
        doc: "The contrast of a foreground on a background, and the levels it meets. Colours are #rgb or #rrggbb.",
        returns: "Report",
        args: { foreground: "string", background: "string" },
      },
      simulate: {
        doc: "The pair as the four colour-vision deficiencies show it (Machado, Oliveira and Fernandes 2009; full severity).",
        returns: "View[]",
        args: { foreground: "string", background: "string" },
      },
      suggest: {
        doc: "The foreground nearest the given one, in perceived lightness (OKLab), that reaches the ratio on the background: the given one when it already does; nothing when no lightness can.",
        returns: "string?",
        async: true,
        args: { foreground: "string", background: "string", target: { type: "f64", doc: "the ratio to reach: 4.5 for AA" } },
      },
    },
  },
});
