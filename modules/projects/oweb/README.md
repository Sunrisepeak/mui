# oweb

A site framework on omui: one semantic tree published as a site that people, search engines and agents each read in their own way.

Label: **preview** (0.1.0). A project of its own on omui, beside the game layer: neither knows the other ([0.1.0 plan](../../../.agents/docs/2026-11-26-release-0.1.0-top-level-design.md) §4.4, D-275; [design](../../../.agents/docs/2026-11-26-oweb-site-framework-design.md)). It is one package: its duties are directories of `src/` (D-99); it is a package because it is a product with its own command.

```sh
oweb site.build --site docs --out dist --languages en,zh --url https://example.org/docs/
oweb routes.list --site docs
oweb page.read --site docs --route /guide/start/        # the page as Markdown: what an agent that only fetches reads
oweb docs.search --site docs --query "first window"     # the sections that answer: route, anchor, trail, the line that says so
oweb mcp                                                # the same operations to an MCP client
```

## What a site is

A directory with `pages/`. Every Markdown file there is a route: `pages/index.md` is `/`, `pages/guide/start.md` is `/guide/start/`, a directory's `index.md` the directory's own. A site of one page has one route and no navigation; a site of many has the navigation its routes give. A first directory named as one of the site's languages (`--languages en,zh`) holds that language's pages.

## What is built

Each route is evaluated once, headless, into its semantic tree — the navigation and the page's document — and the tree is written twice:

| File | What it is | Who reads it |
|---|---|---|
| `index.html` | the whole page as HTML (`omui::semantic::static_html`): headings, links with their addresses, code with its language, tables, pictures; a small default sheet (one column, the reader's fonts, light or dark, plain on paper) | a person before any engine is there, a reader without scripts, a search engine |
| `index.md` | the document as Markdown (`omui::markdown_of`) | an agent that only fetches |

Nothing is written that the tree does not hold, and neither writing loses what the tree holds: a page's HTML read back, and its Markdown parsed again, are the document the page was given (omui's checks, 0.1.0 plan UI1 and UI2; held again here on each built route). A link to another page is written as each writing's own address — the route's directory in HTML, its `index.md` in Markdown; what stands beside the pages (pictures) is copied to the same place.

**Search is one index, made from the tree**: a page's sections — a heading and what stands under it up to the next — each with its route, its anchor, the headings it is under and its text (`search.xdef.json`, `search@1`). A query answers with the sections that hold every word asked for, best first (a word in a title before one in the text; Latin words without case and by their beginning; text written without spaces by its characters in a row), each with the line that says so. A person's search box and an agent's `docs.search` read this one index, so they give the same sections for the same words.

The routes are one table, and the rest is made from it: `routes.xdef.json` (`routes@1`: each route's address, source, title, language, headings and anchors), `llms.txt` (each title beside its Markdown's address), and `sitemap.xml` when the site's address is given.

## Signature features (candidates until their evidence is in)

| Feature | State | Evidence |
|---|---|---|
| One content, three readings: the canvas, the HTML, the Markdown are writings of one tree, and none loses what it holds | first two writings here; the canvas with the engine's load policies (UI3, UI5) | `site_test`: every route's HTML and Markdown read back to its source's document |
| An agent arrives as a person does: an address is enough — it reads `llms.txt`, a page's Markdown, or connects to the page | the fetching door here; the site's operations on the page and the connected door (AG5) | to come with AG5 and AG6 (the cold-start task on a site) |

Not yet here (0.1.0's tasks ST2–ST7): `oweb.config.ts` and routes declared in TypeScript, pages written as views (`*.ts`), layouts and their slots, the search box on a page, code highlighting, the three presets and `omui new`, the engine on a page by its load policy, the delivery manifest and the GitHub Pages adapter.

## Entry points by level

- L0: `oweb site.build --site DIR --out DIR`.
- L1: its options (`--languages`, `--title`, `--url`); `routes.list`, `page.read`.
- L2: the library — `oweb::collect`, `oweb::evaluate`, `oweb::build`, and the tables' writers (`routes_json`, `llms_txt`, `sitemap_xml`).
- L3, L4: a route's tree before it is written, and build stages of one's own: with ST2's layouts and ST3's content pipeline.

## Agent surface

The subject `oweb` (interface `oweb@1`) behind the tool kernel's three entries — the command line, MCP (`oweb mcp`) and the protocol. Its operations are a catalog ([`oweb.ops.xdef.ts`](oweb.ops.xdef.ts), listed in [`src/catalog`](src/catalog)): `site.build` (reversible: delete what was written), `routes.list`, `page.read` and `docs.search` (read). Every answer is data; a failure says why and what the site's routes are. How an agent uses them is [`skill.omui.md`](skill.omui.md). A built site is itself an agent surface: `llms.txt`, each route's `index.md`, `routes.xdef.json` and `search.xdef.json` need no engine and no script.

## Dependencies

omui (`omui.view`, `omui.semantic`, the headless host, the base and Markdown components) and the foundation under it (`mlibs.markdown`, `mlibs.agent`, `mlibs.text.fonts`). Nothing of meng.

## Tests

`tests/oweb/site_test.cpp`: a site of three pages in two languages and a site of one page, built into a scratch directory — the routes and their addresses; every route's `index.html` read back and its `index.md` parsed again against its source's document; links between pages in each writing's own form; the tables; the sections and what a query finds (words, beginnings, Chinese); the command's four operations and what they refuse.
