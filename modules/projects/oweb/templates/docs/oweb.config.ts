// The site's configuration: what it is called, where it is served, its languages, and its pages in the order a
// reader goes through them (left out, every page of pages/ is a route, by address).
import { defineSite } from "oweb/define";

export default defineSite({
  title: "The minimal documents",
  url: "https://example.org/docs/",
  languages: ["en", "zh"],
  routes: [
    { path: "/", source: "pages/index.md" },
    { path: "/start/", source: "pages/start.md" },
    { path: "/ui/", source: "pages/ui.md" },
    { path: "/agent/", source: "pages/agent.md" },
    { path: "/reference/ops/", source: "pages/reference/ops.md" },
    { path: "/zh/", source: "pages/zh/index.md" },
    { path: "/zh/start/", source: "pages/zh/start.md" },
  ],
  budgets: { readable: 100000 },
});
