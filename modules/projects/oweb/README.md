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

A route can also be **an application's page**: `{ path: "/", page: "<directory>" }` in `oweb.config.ts` names the directory an application's page was built into (`omui build --web`: the page and its files). The page is served as it was built, its files beside it, and it is a route like any other — in the navigation by its own `<title>`, in the routes table (`application: true`), in the search and in `llms.txt` — by what its own document says: the first screen a built page holds for a reader without its program (`[data-omui=static]`) is read as Markdown, written as the route's `index.md`, and its links are checked as any page's (an address from where the page is served — `start/` — is a file of its own or a place of the site). Nothing is described twice: no front matter, no metadata beside the page. A page with no such document is not adopted, and two files that would share an address are said.

A site's configuration is the `oweb.config.ts` in its directory — or the one a request names (`--config FILE`): the same pages built as another site, a product's documents behind its own first page. The pages stay the site's (`source`); what such a configuration states beside them (an engine, a route's built page, a catalog) is from its own directory. omui's site is built so: [`site/oweb.config.ts`](../../../site/oweb.config.ts) puts the page `site/` builds first and the minimal documents behind it.

## What is built

Each route is evaluated once, headless, into its semantic tree — the navigation and the page's document — and the tree is written twice:

| File | What it is | Who reads it |
|---|---|---|
| `index.html` | the whole page as HTML (`omui::semantic::static_html`): headings, links with their addresses, code with its language, tables, pictures; a small default sheet (one column, the reader's fonts, light or dark, plain on paper) | a person before any engine is there, a reader without scripts, a search engine |
| `index.md` | the document as Markdown (`omui::markdown_of`) | an agent that only fetches |

Nothing is written that the tree does not hold, and neither writing loses what the tree holds: a page's HTML read back, and its Markdown parsed again, are the document the page was given (omui's checks, 0.1.0 plan UI1 and UI2; held again here on each built route). A link to another page is written as each writing's own address — the route's directory in HTML, its `index.md` in Markdown; what stands beside the pages (pictures) is copied to the same place.

**Search is one index, made from the tree**: a page's sections — a heading and what stands under it up to the next — each with its route, its anchor, the headings it is under and its text (`site.search.xdef.json`, `search@1`). A query answers with the sections that hold every word asked for, best first (a word in a title before one in the text; Latin words without case and by their beginning; text written without spaces by its characters in a row), each with the line that says so. A person's search box and an agent's `docs.search` read this one index with one function (`oweb.shell`'s `search`, carried by the builder and by the program a page loads), so they give the same sections for the same words. Every route's head says where the index is (`<link rel="search" type="application/json" href="…site.search.xdef.json">`); a page with a program has the box (the shell's part `search`), which fetches the index when words are first typed and answers each change from it — a link per section, to its anchor, with the headings it is under and the line that holds the words. A page without a program has no box: nothing would answer it.

The routes are one table, and the rest is made from it: `site.routes.xdef.json` (`routes@1`: each route's address, source, title, language, headings and anchors), `llms.txt` (each title beside its Markdown's address), and `sitemap.xml` when the site's address is given.

**What a visitor downloads is counted, and held**: the build's last file is the delivery manifest (`site.delivery.xdef.json`, `delivery@1`, by [`mlibs.delivery`](../../base/delivery)) — every file as stored and as a static host sends it (gzip for text, as stored for pictures), and one budget: a route's bytes before it reads, its `index.html` as sent, at most 100,000 (`--readable`; the page carries its style and needs nothing else to read). A site over it is written all the same and the build fails saying which route and by how much. The sample of seven documents: at most 1,926 B a route. A site with a program has its engine's share counted beside it: the program (`engine/*`: its launcher and its code, as sent — 2,670,071 B for the documents template), the faces its pages are set in (their cuts, 344,482 B) and the whole faces fetched when text asks (3,249,054 B); the program is held to what the site states (`budgets: { program: N }` in `oweb.config.ts`; none stated: counted only). For GitHub Pages: `.nojekyll`, so the directory is served as it is, and `404.html`, the page a lost address is answered with — the site's own `pages/404.md` or one that says so and leads home; it is served at any address, so its links are from the site's own address (the path of `url`, said by the document's `<base>`), and no table lists it.

## Presets

A site is a package: its build program (`build.mcpp`) is one line — `oweb::rules::site()` of the build rule [`oweb.rules`](rules) — and `mcpp build` (so `omui build`) writes the site into `target/site`, again whenever a page changed. `omui new NAME --template oweb:docs` makes one (mcpp's package templates: `templates/` here).

| Preset | What it starts from |
|---|---|
| `docs` | pages in Markdown, two languages, a generated reference, a live example: the minimal documents |

What the preset `docs` starts from is the minimal documents (`templates/docs`): the one corpus 0.1.0 checks documents with (0.1.0 plan D-274; oweb's design §3.6): every kind of content a documentation site carries — headings of three levels, paragraphs with every inline format, ordered and nested lists, a task list, tables with alignment, code in three languages and one block with none, a picture, a quotation, an HTML comment, anchors within a page and links between pages, two languages, a reference generated from a catalog, a page with a live example (`pages/live.md`: a view document, shown running above its source).

It is used five ways: the template's content, the round-trip checks (`tests/site/template_test.cpp`: every route's HTML read back and its Markdown parsed again are its source's document), the search's checks, the questions of the cold-start evaluation, and the content both sides build when oweb is compared side by side.

`oweb.config.ts` states the site's title, address, languages and its pages in the order a reader goes through them. `pages/404.md` is the page a lost address is answered with (no route). The route `/reference/ops/` has no page of its own: the configuration names a catalog and each build generates the page from it — here `reference: "oweb"`, the catalog of the command that builds the site, carried with it (a product's own catalog is its file: `reference: "../tool/tool.ops.xdef.ts"`); pages link to it by its address (`/reference/ops/`). The corpus' long page is not a file here: `template_test` makes it of the repository's own guides (`docs/en/guide`, seven as one page), so nothing is copied.

```sh
oweb site.build --site modules/projects/oweb/templates/docs --out /tmp/docs
```

The presets `page` (one page, a view) and `site` (a first page that is a view, and documents) follow: a site whose first page is an application is stated today by a route's `page` (above), the application built by its own package.

## Signature features (candidates until their evidence is in)

| Feature | State | Evidence |
|---|---|---|
| One content, three readings: the canvas, the HTML, the Markdown are writings of one tree, and none loses what it holds | first two writings here; the canvas with the engine's load policies (UI3, UI5) | `site_test`: every route's HTML and Markdown read back to its source's document |
| An agent arrives as a person does: an address is enough — it reads `llms.txt`, a page's Markdown, or connects to the page | the fetching door here; the site's operations on the page and the connected door (AG5) | to come with AG5 and AG6 (the cold-start task on a site) |

**Side by side** ([`tests/bench/competitors/sites`](../../../tests/bench/competitors/sites/README.md): the documents template built by oweb and by VitePress, Starlight and mdBook at their defaults, each route visited cold in headless Chromium on Lighthouse's mobile profile; 2026-11-27). A route of oweb's is one request of 2.4 kB and paints at 238 ms: against the strongest of the three, Starlight, 22× fewer bytes (53.9 kB), 10× fewer requests and 2.0× sooner (474 ms); the build takes 27 ms where mdBook's takes 41 (1.5×); only oweb writes each route's Markdown and `llms.txt`. Behind: the same route with its engine takes 2.45 MB, 46× Starlight's, and its program has drawn it after 13 s on that connection (red 166) — which is why a page takes the program by the route's choice, not by default <!-- claim:oweb.docs-route -->.

**A page's own parts are made from the routes table**, each a navigation landmark of the tree (so of the HTML): the site's pages (its language's, in the routes' order, then each other language: this page in it where the site has it — `/start/` and `/zh/start/`, the first language without a part — that language's first page where not; a page with more than one language says so to a search engine, `hreflang`); the page's contents — its sections of the second and third level as links to their anchors, where it has two or more; the page before and the page after among its language's. Their names are said in the page's language. In a wide window the default sheet sets the pages at the left and the contents at the right, both staying as the page scrolls; in a narrow one they stand above the document; on paper they are left out.

**A site states what it is in `oweb.config.ts`** (`defineSite` from `oweb/define`; declared data of kind `site@1`, evaluated once to its snapshot): its title, where it is served, its languages, a budget, and — when the order or the addresses are not the directory's — its routes: these pages and no others, in this order, each served where it says and called what it says. A field that is none of the kind's is a problem at its place, naming the fields the kind has. A request's options stand over the configuration's. **A reference is generated, not copied**: a stated route may name an operation catalog instead of a page (`reference: "../tool/tool.ops.xdef.ts"`); each build reads the catalog and writes its reference as that route's page, so it cannot go stale (the catalog's own generator, `mlibs.control.gen`, writes the text). **A link may be by the site's own address** — `/reference/ops/`, `/media/mark.svg` — and is written as each writing's, like a link by a page's file. **A link inside the site that leads nowhere is not built**: a page that is no route, an anchor its page has not, a picture that is not there — each said with its page, and nothing is written.

**A page may look as the engine draws it** (`look: true` in the configuration; UI5 step 4, preview): each route is drawn once, light and dark, and its `index.html` says what the frame drew — every element its node's layout (the view's flexbox and grid as CSS), the face, size, colour and spacing of its text, its ground, corners, borders and shadows (`omui.semantic`'s look) — in place of the default sheet. The faces are the site's own files (`assets/omui-<family>.ttf`), asked for by each page from where it is served, each cut to the characters the site's pages show ([`mlibs.text.subset`](../../content/text/subset); the documents template: 5.2 MB of faces to 0.84 MB, 0.35 MB as sent — glyph ids are kept, so a face used for two characters is still 0.38 MB: the look stays opt-in until that is cut too). The document is the same: its HTML read back and its Markdown are those of the page without a look. A browser lays the page out by the same model at any width, with no program; when the engine is on a page (not yet) the canvas shows what the page showed.

**A page may load the program that draws it** (`engine: "<a world's directory>"` in the configuration, or `--engine DIR`; ST2 step 5, preview). A world is a program packed as a page waiting for its document (`omui build --web` of [`oweb.reader`](reader); `omui-gen dist-toy --world`). The site serves its files once, from `engine/`, and every page is made from the world's page with the page's own document in it: it reads with no script exactly as before, and takes the program when its route says — `load: "eager"` at once (the default), `"idle"` once the page has loaded and is idle, `"interaction"` when the reader first operates something only a program answers (reading never takes it: scrolling, the browser's find, a selection, a link); the site's `load`, or a stated route's own; the routes table says each route's. The program is sent nothing else: it reads the page's own HTML back — the navigation's links, the document's markup — and makes the view the builder made ([`oweb.shell`](shell)), so the layer over the canvas takes the written elements over one for one (D-284). Such a site's pages are written with their look, in a view of the window's size that scrolls (a canvas is not scrolled by its window); the page for a lost address takes no program.

**A page's pictures are the page's**: the program asks the page for a picture's pixels — the `<img>` its document already holds, decoded by the browser — before it would read a file, so the reader's program carries no decoder and asks for nothing twice; the page waits for its document's pictures before it takes the program, so the first frame has them. As the pages are written, the builder gives each picture its size from its file (an SVG's from its `width` and `height`, else its `viewBox`); a picture alone in a paragraph is its own size, narrowed where the column is narrower.

**A page's faces are the page's**: the site's cuts of the faces a page is set in (`assets/omui-<family>.ttf`, fetched for the document already) are the program's first faces, said to the world's page with the page; the world's whole faces — and a face the document is not set in — are fetched only when a character the cuts lack is wanted (a cut keeps ASCII and `…`, which a view draws beside its text). So the program's first frame is set in the faces the page was, and no font is fetched for the takeover: on the sample, 5,170,260 B of faces after the first frame before this, 0 after.

Measured on the documents template (headless Chromium 159, `omui-devtools check takeover --path`, eight routes at three widths, at the top and scrolled): 34 of 48 rows within UI5's bound — a word moves at most 2 px, half of them at most 1 px, none to another line — with every word matched by its node's id, `/`, `/start/`, `/ui/`, `/agent/` and `/live/` all six rows each (median 0.13–0.41 px, most 0.91 px), `/live/`'s example answering when pressed on the canvas. Outside it: a table that never narrows (`/reference/ops/`, red 162) and lines that mix Chinese and Latin words (`/zh/`, two rows of `/zh/start/`: the engine sets each line by the faces it uses). The reader's program with view documents: 7,920,429 B (2,596,334 as sent).

**A navigation may be one's own** (ST2 step 6): a page's view is a function of its parts ([`oweb.shell`](shell)'s `View`) — the shell's own page, or a site's: the pages at the side, the contents beside the document, a part left out, from the pieces the shell's page is made of. A site with its own view builds with a program of its own — `oweb::Sites sites{view};` behind the same entries as `oweb` — and gives the same view to the program its pages load; the parts are read back from a page by their names wherever the view placed them, so the page as written is the page as drawn. Held on the documents template with such a view: every route's Markdown the same bytes, its document's HTML the same but for where it stands, the page read back and written again the same.

**A page may hold a live example** (ST3 step 3): a block of the language `view` is a view document ([`omui.viewdoc`](../omui/viewdoc)) — an interface as data — and the page shows it running above its source. With no program the example is there as it starts (its buttons, its text; marked `data-output`, so the document read back is the block alone); with the program it answers — the same nodes — and the program needs no script engine for it. `index.md` has the block as written: a view document an agent can check and run. An example whose text is no view document fails the build, saying what is wrong and where. The sample's `/live/` (a counter) takes the program once the page is idle (`load: "idle"`).

**A long page holds**: the repository's seven guides as one page (5,928 words) are written in 23,090 B as sent with their look (the budget 100,000), read back to their document, and taken over 15,000 px down with a word moving 0.4 px at the median — the distance does not grow with the page. Scrolling it in a browser drops frames (red 164, open).

Not yet here (0.1.0's tasks ST2–ST7): pages written as views (`*.ts`), an example written in TypeScript, a layout written in TypeScript (`layouts/`; a view in C++ is here), a picture drawn at the screen's own density (the page's pixels are its natural size's), code highlighting, the three presets and `omui new`, the window itself scrolling a page that has a program, the program cut to what reading needs (2.5 MB as sent now), the GitHub Pages action (the Pages workflow builds the documents template with its engine and checks three routes).

## Entry points by level

- L0: `oweb site.build --site DIR --out DIR`.
- L1: the site's `oweb.config.ts`, and a request's options over it (`--languages`, `--title`, `--url`, `--readable`, `--engine`, `--load`); `routes.list`, `page.read`, `docs.search`.
- L2: the library — `oweb::configured`, `oweb::collect`, `oweb::evaluate`, `oweb::build`, and the tables' writers (`routes_json`, `llms_txt`, `sitemap_xml`, `search_json`).
- L3, L4: a route's tree before it is written, and build stages of one's own: with ST2's layouts and ST3's content pipeline.

## Agent surface

The subject `oweb` (interface `oweb@1`) behind the tool kernel's three entries — the command line, MCP (`oweb mcp`) and the protocol. Its operations are a catalog ([`oweb.ops.xdef.ts`](oweb.ops.xdef.ts), listed in [`src/catalog`](src/catalog)): `site.build` (reversible: delete what was written), `routes.list`, `page.read` and `docs.search` (read). Every answer is data; a failure says why and what the site's routes are. How an agent uses them is [`skill.omui.md`](skill.omui.md). A built site is itself an agent surface: `llms.txt`, each route's `index.md`, `site.routes.xdef.json`, `site.search.xdef.json` and `site.delivery.xdef.json` need no engine and no script.

## Packages

- `oweb` (this directory): the builder, the command and its catalog.
- [`oweb.rules`](rules): the build rule — a package that is a site has its site written at each build.
- [`oweb.reader`](reader): the program a site's pages load in a browser — it reads the page's own HTML back and draws it; one program for every page.
- [`oweb.shell`](shell): a page's shell as a view — one function for the builder and for the program a page loads, so a node has one id in the page as written and as drawn.

## Dependencies

omui (`omui.view`, `omui.semantic`, the headless host, the base and Markdown components) and the foundation under it (`mlibs.markdown`, `mlibs.agent`, `mlibs.text.fonts`). Nothing of meng.

## Tests

`tests/site/site_test.cpp`: a site of three pages in two languages and a site of one page, built into a scratch directory — the routes and their addresses; every route's `index.html` read back and its `index.md` parsed again against its source's document; links between pages in each writing's own form; the tables; the sections and what a query finds (words, beginnings, Chinese); the command's four operations and what they refuse.
