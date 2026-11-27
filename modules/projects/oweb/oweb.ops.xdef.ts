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
  doc: "A site states its title, address, languages, routes and budget in its oweb.config.ts; every operation reads it and a request's options stand over it. Build a directory of pages into a site whose every route is its semantic tree written as HTML and as Markdown; list a site's routes; read one page as an agent that only fetches would, and search every page's sections, without building the site.",
  generated: "src/catalog",
  operations: [
    {
      name: "site.build",
      effect: "reversible",
      summary: "Build the site: each page of pages/ a route with its index.html (the tree as HTML: readable with no script) and index.md (the tree as Markdown), the routes table, llms.txt, the search index, 404.html for a lost address, given the site's address sitemap.xml, and the delivery manifest — every file as stored and as a static host sends it, each route's bytes before it reads held to a budget (over it: written, and failed with where). A link inside the site that leads nowhere fails the build with each one said, and nothing is written",
      params: [
        { name: "site", type: "path", doc: "the site's directory (it holds pages/)" },
        { name: "config", type: "path", doc: "the site's configuration, where it is not the oweb.config.ts in its directory: the same pages as another site — what it states beside the pages (an engine, a route's built page, a catalog) is from its own directory", optional: true },
        { name: "out", type: "path", doc: "the directory written" },
        { name: "title", type: "string", doc: "the site's title (default: its first page's)", optional: true },
        { name: "languages", type: "string", doc: "its languages, the default first: en,zh — a first directory of that name is that language's pages", optional: true },
        { name: "url", type: "string", doc: "where it is served (https://example.org/docs/): the sitemap's addresses", optional: true },
        { name: "readable", type: "string", doc: "the most bytes a route sends before it reads, its index.html as gzip (default 100000; 0 holds none)", optional: true },
        { name: "engine", type: "path", doc: "the program its pages load in a browser: a world's directory (omui build --web of oweb.reader) — served once from engine/, each page still its document until it takes the program", optional: true },
        { name: "load", type: "string", doc: "when a page takes the program where its route does not say: eager (default), idle, interaction", optional: true },
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
        { name: "config", type: "path", doc: "the site's configuration, where it is not the oweb.config.ts in its directory: the same pages as another site — what it states beside the pages (an engine, a route's built page, a catalog) is from its own directory", optional: true },
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
        { name: "config", type: "path", doc: "the site's configuration, where it is not the oweb.config.ts in its directory: the same pages as another site — what it states beside the pages (an engine, a route's built page, a catalog) is from its own directory", optional: true },
        { name: "route", type: "string", doc: "its address (/guide/start/)" },
        { name: "as", type: "string", doc: "markdown (default) or html", optional: true },
        { name: "languages", type: "string", doc: "the site's languages, the default first", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "docs.search",
      effect: "read",
      summary: "The sections of a site's pages that hold every word asked for, best first: each its route, its anchor, the headings it is under and the line that says so — the same index a built site serves as site.search.xdef.json",
      params: [
        { name: "site", type: "path", doc: "the site's directory" },
        { name: "config", type: "path", doc: "the site's configuration, where it is not the oweb.config.ts in its directory: the same pages as another site — what it states beside the pages (an engine, a route's built page, a catalog) is from its own directory", optional: true },
        { name: "query", type: "string", doc: "the words; text without spaces between words matches by characters in a row" },
        { name: "limit", type: "string", doc: "how many at most (default 8)", optional: true },
        { name: "languages", type: "string", doc: "the site's languages, the default first", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
});
