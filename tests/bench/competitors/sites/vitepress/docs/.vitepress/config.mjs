// VitePress's side of `bench sites`: its default theme, two locales, the pages in the order the corpus states
// them, its own local search. The pages are put in docs/ by the harness, as they are.
import { defineConfig } from "vitepress";

const en = [
  { text: "The minimal documents", link: "/" },
  { text: "Start", link: "/start" },
  { text: "Interfaces", link: "/ui" },
  { text: "Agents", link: "/agent" },
  { text: "A live example", link: "/live" },
  { text: "oweb operations", link: "/reference/ops/" },
];
const zh = [
  { text: "最小文档集", link: "/zh/" },
  { text: "开始", link: "/zh/start" },
];

export default defineConfig({
  title: "The minimal documents",
  locales: {
    root: { label: "English", lang: "en", themeConfig: { sidebar: en } },
    zh: { label: "简体中文", lang: "zh", themeConfig: { sidebar: zh } },
  },
  themeConfig: { search: { provider: "local" } },
});
