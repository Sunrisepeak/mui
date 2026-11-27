// Starlight's side of `bench sites`: its default theme, two locales, the pages in the order the corpus states
// them, its own search (Pagefind). The pages are put in src/content/docs/ by the harness, each with its title in
// front matter and its links between pages as routes.
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

export default defineConfig({
  integrations: [
    starlight({
      title: "The minimal documents",
      defaultLocale: "root",
      locales: { root: { label: "English", lang: "en" }, zh: { label: "简体中文", lang: "zh-CN" } },
      sidebar: [
        { label: "Start", link: "/start/" },
        { label: "Interfaces", link: "/ui/" },
        { label: "Agents", link: "/agent/" },
        { label: "A live example", link: "/live/" },
        { label: "oweb operations", link: "/reference/ops/" },
      ],
    }),
  ],
});
