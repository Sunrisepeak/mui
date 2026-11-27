// A site's reading operations as its own page answers them (0.1.0 plan AG5; oweb's design §4.10): the subject
// `site` a page's program adds to its runtime — asked through the page's tools (WebMCP) or a connection to the
// page (`omui connect`). They are the `oweb` command's reading operations without `site`: the site is the one the
// page is of, its files fetched by the page when first asked for. ops-gen makes the catalog
// (src/catalog/catalog.cppm), the listing (site.ops.json) and the reference (site.ops.md); effects are openas's.
import { defineOps } from "mlibs/define";

export default defineOps({
  product: "site",
  module: "oweb.reader",
  namespace: "oweb::reader",
  doc: "The site this page is of, asked from inside the page: what it is, its routes, the sections that answer a question, a page as it was written. The answers are the ones the `oweb` command gives of the site as built and the ones its static files hold (llms.txt, each route's index.md, site.search.xdef.json) — whichever door an agent comes by, a site says the same. Its files are fetched when first asked for, once; a reader who asks nothing pays for none.",
  generated: "src/catalog",
  tasks: [
    {
      name: "read-the-site",
      summary: "What the site is and holds, the sections that answer a question, then a page as Markdown",
      steps: [
        { op: "site.describe" },
        { op: "docs.search", query: "install" },
        { op: "page.read", route: "/" },
      ],
    },
  ],
  operations: [
    {
      name: "site.describe",
      effect: "read",
      summary: "What the site is, as it says it at each of its doors: its title, address and languages, each route with its title and its Markdown's address, the requests to go on with — and the text its build wrote as llms.txt",
      params: [],
      result: "json",
      cost: "bounded",
    },
    {
      name: "routes.list",
      effect: "read",
      summary: "The site's routes: each its address, source, title, language and headings with their anchors",
      params: [],
      result: "json",
      cost: "bounded",
    },
    {
      name: "page.read",
      effect: "read",
      summary: "One route as it was written: its Markdown (what an agent that only fetches reads) or its HTML",
      params: [
        { name: "route", type: "string", doc: "its address in the site (/guide/start/)" },
        { name: "as", type: "string", doc: "markdown (default) or html", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
    {
      name: "docs.search",
      effect: "read",
      summary: "The sections of the site's pages that hold every word asked for, best first: each its route, its anchor, the headings it is under and the line that says so. The words are also put in the page's search box: who reads the page sees what was asked and what was found",
      params: [
        { name: "query", type: "string", doc: "the words; text without spaces between words matches by characters in a row" },
        { name: "limit", type: "string", doc: "the most sections answered (default 8)", optional: true },
      ],
      result: "json",
      cost: "bounded",
    },
  ],
});
