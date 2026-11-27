// omui's site as one site (0.1.0 plan ST7): its first page is this package's page — an application, built by
// `omui build --web` — and its documents are the minimal documents (oweb's `docs` template: the one corpus 0.1.0
// checks documents with, D-274), the same files, not a copy:
//
//   oweb site.build --site modules/projects/oweb/templates/docs --config site/oweb.config.ts --out <directory>
//
// What a configuration states beside the pages is from its own directory: `page` is where the Pages lane puts
// the built page (target/site).
import { defineSite } from "oweb/define";

export default defineSite({
  title: "omui",
  url: "https://sunrisepeak.github.io/mui/",
  languages: ["en", "zh"],
  routes: [
    { path: "/", page: "../target/site" },
    { path: "/docs/", source: "pages/index.md", title: "Documents" },
    { path: "/start/", source: "pages/start.md" },
    { path: "/ui/", source: "pages/ui.md" },
    { path: "/agent/", source: "pages/agent.md" },
    { path: "/live/", source: "pages/live.md", load: "idle" },
    { path: "/reference/ops/", reference: "oweb" },
    { path: "/zh/", source: "pages/zh/index.md" },
    { path: "/zh/start/", source: "pages/zh/start.md" },
  ],
  budgets: { readable: 100000 },
});
