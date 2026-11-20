// The site's one page: how far agents may go, what the recorder keeps, and the design it ships — played in place
// when the Live Design key is pressed (generate.replay). Nothing else of the page is open to a visitor's agent in
// this round; the theme as data and a surface for its work come with the bridge (G5, the site's design §4).
import { defineConfig } from "omui/define";

export default defineConfig({
  agent: { development: "development", release: "dual-user" },
  record: { development: "full", release: "errors" },
  designer: { development: true, release: false },
  generation: { replay: "design/main.replay.xdef.json" },
});
