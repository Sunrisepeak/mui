// The site's one page: how far agents may go, what the recorder keeps, and the design it ships — played in place
// when the Live Design key is pressed (generate.replay). Its designer ships with it: dormant until the person links
// their own agent (Copy, `omui connect <code>`), then that agent designs the page live in their browser — the
// framework's Live Design (design.*), nothing of it written here; a reload shows the page as it ships.
import { defineConfig } from "omui/define";

export default defineConfig({
  agent: { development: "development", release: "dual-user" },
  record: { development: "full", release: "errors" },
  designer: { development: true, release: true },
  // Its course on the mark's O (the path whose id is "o"): the progress goes round the O, scrubbed along it.
  generation: { replay: { file: "design/main.replay.xdef.json", course: "o" } },
});
