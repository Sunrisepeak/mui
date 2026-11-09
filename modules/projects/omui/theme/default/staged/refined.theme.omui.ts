// Staged values (the component quality checklist's way): an overlay over the
// default theme, loaded only where it is reviewed (site/tests/quality_lab_test.cpp
// draws the defaults beside it) — never part of the default library, so no
// conformance trace and no pin sees it. Approved, its values move into light
// and dark in one checkpoint and this file goes.

import { defineTheme } from "omui/define";

export default defineTheme({
  refined: {
    doc: "Refined (staged, B4 second pass): the state's halo round a switch's thumb, a checkbox and a slider's thumb — under the pointer and pressed — in the control's own colour, as the side-by-side review against Material 3 asked (compare-v1, 2026-11-09). Merged into light and dark once approved.",
    overlay: true,

    spacing: {
      switch_press_stretch: 6,
    },

    shadows: {
      "switch_halo:hovered": { x: 0, y: 0, blur: 0, spread: 7, color: "#4f46e5 / 0.08", tint: 1 },
      "switch_halo:pressed": { x: 0, y: 0, blur: 0, spread: 12, color: "#4f46e5 / 0.24", tint: 1 },
      "check_halo:hovered": { x: 0, y: 0, blur: 0, spread: 11, color: "#4f46e5 / 0.08", tint: 1 },
      "check_halo:pressed": { x: 0, y: 0, blur: 0, spread: 11, color: "#4f46e5 / 0.16", tint: 1 },
      "thumb_halo:hovered": { x: 0, y: 0, blur: 0, spread: 7, color: "#4f46e5 / 0.08", tint: 1 },
      "thumb_halo:pressed": { x: 0, y: 0, blur: 0, spread: 12, color: "#4f46e5 / 0.22", tint: 1 },
    },
  },
});
