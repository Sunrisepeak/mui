// A site that states its routes: these pages, in this order; pages/draft.md is no route.
import { defineSite } from "oweb/define";

const base: string = "https://example.org/stated/";

export default defineSite({
  title: "Stated",
  url: base,
  languages: ["en"],
  routes: [
    { path: "/", source: "pages/index.md" },
    { path: "/start/", source: "pages/guide/start.md", title: "Start here" },
    { path: "/about/", source: "pages/about.md" },
  ],
  budgets: { readable: 50000 },
});
