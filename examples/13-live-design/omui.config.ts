// What a release lets a user's agent change, and the design it ships (D-144,
// G8): the plans page takes offers in its `extras` place and a new look (an
// overlay of the theme's tokens) as data — previewed, committed, undone,
// kept, exported; and plays how it was designed, in place (generate.replay).
import { defineConfig } from "omui/define";

export default defineConfig({
  agent: { development: "development", release: "dual-user" },
  record: { development: "full", release: "errors" },
  designer: { development: true, release: false },
  generation: {
    open: { release: "data" },
    surfaces: {
      extras: "offers and notes a user's agent adds under the plans",
      look: { kind: "theme", doc: "the page's colours, type and spacing a user's agent may change" },
    },
    replay: "design/main.replay.xdef.json",
  },
});
