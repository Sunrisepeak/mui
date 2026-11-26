// oweb's operations (0.1.0 plan §4.4, D-275): what a person at a terminal or an Agent does with a site without
// opening a page, through the `oweb` command — the subject `oweb` behind the tool kernel's entries
// (`oweb site.build --site docs --out dist`, `oweb mcp`; records out, exit 0 done, 1 failed with why). ops-gen
// makes the catalog (src/catalog/catalog.cppm), the listing (oweb.ops.json) and the reference (oweb.ops.md);
// effects are openas's.
import { defineOps } from "mlibs/define";

export default defineOps({
  product: "oweb",
  module: "oweb",
  namespace: "oweb",
  doc: "Build a directory of pages into a site whose every route is its semantic tree written as HTML and as Markdown; list a site's routes; read one page as an agent that only fetches would, without building the site.",
  generated: "src/catalog",
  operations: [
    {
      name: "site.build",
      effect: "reversible",
      summary: "Build the site: each page of pages/ a route with its index.html (the tree as HTML: readable with no script) and index.md (the tree as Markdown), the routes table, llms.txt and, given the site's address, sitemap.xml",
      params: [
        { name: "site", type: "path", doc: "the site's directory (it holds pages/)" },
        { name: "out", type: "path", doc: "the directory written" },
        { name: "title", type: "string", doc: "the site's title (default: its first page's)", optional: true },
        { name: "languages", type: "string", doc: "its languages, the default first: en,zh — a first directory of that name is that language's pages", optional: true },
        { name: "url", type: "string", doc: "where it is served (https://example.org/docs/): the sitemap's addresses", optional: true },
      ],
      result: "json",
      cost: "bounded",
      undo: "delete the directory written",
    },
    {
      name: "routes.list",
      effect: "read",
      summary: "The routes a site's pages give: each its address, source, title, language and headings with their anchors",
      params: [
        { name: "site", type: "path", doc: "the site's directory" },
        { name: "languages", type: "string", doc: "its languages, the default first", optional: true },
      ],
      result: "json",
      cost: "instant",
    },
    {
      name: "page.read",
      effect: "read",
      summary: "One route as it would be written, without building the site: its Markdown (what an agent that only fetches reads) or its HTML",
      params: [
        { name: "site", type: "path", doc: "the site's directory" },
        { name: "route", type: "string", doc: "its address (/guide/start/)" },
        { name: "as", type: "string", doc: "markdown (default) or html", optional: true },
        { name: "languages", type: "string", doc: "the site's languages, the default first", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
});
