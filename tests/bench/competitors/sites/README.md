# Sites of documents, side by side

One corpus — [the minimal documents](../../../../modules/projects/oweb/templates/docs) (seven pages in two languages and the generated reference: eight routes) — built into a static site by [oweb](../../../../modules/projects/oweb) and by VitePress, Astro Starlight and mdBook, each at its pin, and measured from outside (0.1.0 plan ST7; D-135: side by side, not scored).

```sh
omui-devtools bench sites                      # every side; npm installs their pins into target/bench-sites
omui-devtools bench sites --no-install --ledger
```

## The sides

| Side | What builds it | As set up |
|---|---|---|
| `oweb` | the `oweb` command of this repository, a release build | documents alone: no program is loaded (the default) |
| `oweb-engine` | the same, with `--engine` (the newest world of [`oweb.reader`](../../../../modules/projects/oweb/reader) packed here) | every page written as its frame looks, and taking the program that draws and operates it once opened |
| `vitepress` | VitePress 1.6.4 ([vitepress/](vitepress)) | its default theme, two locales, its local search |
| `starlight` | Astro 7.4.13 with Starlight 0.43.2 ([starlight/](starlight)) | its default theme, two locales, its search (Pagefind) |
| `mdbook` | mdBook 0.5.2 ([mdbook/](mdbook)) | its default theme and search |

Each of their projects holds its configuration only. The harness copies it to `target/bench-sites/<side>` and puts the corpus in, in the form the side reads: as the pages are (VitePress, mdBook), or with each page's first heading as its title in front matter and its links between pages as routes (Starlight follows no link to a `.md` file). The words are the same on every side; the generated reference is the page oweb's own build generates (`src/catalog/oweb.ops.md`).

## What is measured

[`sites.bench.xdef.json`](sites.bench.xdef.json) states the routes, the pins and the visit's conditions. A round is, for each side in turn:

- **the build** — the output removed, then the side's own command (`oweb site.build`, `npm run build`, `mdbook build`): its wall time, the median of three in a row (a build of tens of milliseconds is otherwise the machine's moment);
- **a cold visit of each route** in headless Chromium, a browser of its own each (no cache), on Lighthouse's mobile profile (1.6 Mbps, 150 ms round trip, the processor slowed 4×) in a phone's viewport, until the page has loaded, nothing it asked for is still coming and nothing has arrived for two seconds — and, where the page takes a program, until the program has drawn it. Counted: the bytes transferred (the protocol's `encodedDataLength`), the requests, what the first paint waits for (the document's transfer and that of every resource the browser marks `renderBlockingStatus: blocking`), the first and the largest contentful paint, the time long tasks blocked (each task's time over 50 ms).

A round's number is the median over the eight routes; a side's, the median of the rounds. The first round also measures what was written ([`mlibs.delivery`](../../../../modules/base/delivery): each file as stored and as a static host sends it) and visits each route with scripts off: are the words a reader came for in the page. What an agent that only fetches finds is said as facts of the written site: a route's Markdown beside it, `llms.txt`, a table of the routes, the search's index as data.

`--ledger` judges the rounds' samples into the lead ledger (the metrics `site.*` of the project `oweb` in [`lead.metrics.xdef.json`](../lead.metrics.xdef.json)): the ratio a round, its median and a 95 % bootstrap interval, against the strongest of the three.

## The numbers

Measured 2026-11-27 at `f19dd3e7` (Linux x64, headless Chromium 159; five rounds; 1-minute load 8 at the start and 6 at the end — the times move with it, the bytes do not). Each number is the median of the rounds:

| side | build (ms) | files | stored (kB) | a route transfers (kB) | its first paint waits for (kB) | requests | first paint (ms) | largest paint (ms) | blocked (ms) | its program has drawn (ms) | reads with no script | the browser's find reaches its words | Markdown routes | llms.txt |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| oweb | 27 | 24 | 91.0 | 2.4 | 2.5 | 1 | 238 | 238 | 0 | — | 8 of 8 | 8 of 8 | 8 | yes |
| oweb-engine | 689 | 36 | 12407.6 | 2453.6 | 14.4 | 9 | 242 | 242 | 258 | 13148 | 8 of 8 | 8 of 8 | 8 | yes |
| vitepress | 1572 | 48 | 1180.6 | 163.8 | 25.1 | 13 | 884 | 884 | 0 | — | 8 of 8 | 8 of 8 | 0 | no |
| starlight | 1006 | 53 | 1234.8 | 53.9 | 18.4 | 10 | 474 | 496 | 0 | — | 8 of 8 | 8 of 8 | 0 | no |
| mdbook | 41 | 44 | 1129.5 | 99.1 | 25.9 | 16 | 608 | 608 | 0 | — | 8 of 8 | 8 of 8 | 0 | no |

As the ledger judges them ([the lead pages](../../../../docs/en/lead.md), project `oweb`): the ratio against the strongest of the three, its 95 % interval, the verdict by the interval's lower end.

| metric | against | ratio | verdict |
|---|---|---:|---|
| `site.route.transfer.bytes` | Starlight | 22.2× | far ahead |
| `site.route.requests` | Starlight | 10.0× | far ahead |
| `site.written.bytes` | mdBook | 12.4× | far ahead |
| `site.route.blocking.bytes` | Starlight | 7.3× | ahead |
| `site.route.lcp.mobile.ms` | Starlight | 2.1× [2.1, 2.5] | ahead |
| `site.route.fcp.mobile.ms` | Starlight | 2.0× [2.0, 2.2] | ahead |
| `site.build.ms` | mdBook | 1.5× [1.4, 1.5] | ahead |
| `site.engine.route.transfer.bytes` | Starlight | 0.022× | behind: the program a page takes (red 166) |
| `site.engine.build.ms` | mdBook | 0.059× | behind: 689 ms against 41 (not a core metric: where the time goes is not yet taken apart) |

What the numbers are of:

- **A route of oweb's is one request.** The page is its document — its HTML with its sheet inside, 2.4 kB as sent — and nothing else is asked for: no script, no font, no style sheet. Theirs each send a theme's sheets, fonts and scripts beside the document (10 to 16 requests, 54 to 164 kB), of which the first paint waits for 18 to 26 kB.
- **The same page with its engine paints as early** (242 ms: it is written before the program), then takes 2.45 MB — 46 times Starlight's route — and its program has drawn it 13.1 s after the visit began on this connection. That is red 166 (3.39 MB and 17.9 s before the world was packed for size and in two parts), and why the engine is a site's choice a route (`load`), not the default.
- **Every side's words are in the page without scripts** (8 of 8 each): all four write their pages ahead of time. With scripts, the browser's own find reaches a phrase of each page on every side — on oweb's with its engine too, once the program has the page (red 167: it found single words only, before the layer set a text as one text).
- **Only oweb writes what an agent that only fetches reads**: each route's Markdown beside it and `llms.txt`, from the same tree as the page. As set up here — their defaults — the others write neither.
- **The build**: 27 ms against mdBook's 41, both a process's start for eight routes. An earlier run of this day (five single builds, the machine at load 19) could not tell them apart — one round of five had oweb the slower, and the ledger judged it behind by the interval's lower end; a round times three builds since.

## A thousand pages

Eight routes are a build's start: every side's time above is mostly its own start-up. How a build grows with the site is measured on a generated corpus:

```sh
omui-devtools bench sites --no-install --scale 1000            # 1,000 pages and a first page
omui-devtools bench sites --no-install --scale 1000 --ledger
```

The pages are three of the documents' (`start.md`, `ui.md`, `agent.md`: their words as they are, each its own title, their links between pages and their pictures left out so that one text serves every side), in directories of 50. Each side builds them with the navigation its own starter states for a site of that shape: mdBook a chapter a directory in `SUMMARY.md`, VitePress a collapsed sidebar group a directory, Starlight a collapsed group generated from each directory, oweb nothing (its navigation is the routes'). A build is from nothing written; three in a row, the median. One page deep in the site (`/s03/p0175/`) is read as each side wrote it.

Measured 2026-11-27 (Linux x64, the same pins; 1-minute load 2 to 4):

| side | build (ms) | ms a page | files | stored (kB) | a page stored (B) | a page sent (B) | links in the page |
|---|---:|---:|---:|---:|---:|---:|---:|
| oweb | 1261 | 1.26 | 2008 | 15427.8 | 10662 | 2638 | 74 |
| vitepress | 32418 | 32.42 | 3026 | 409826.8 | 401336 | 26964 | 1007 |
| starlight | 3714 | 3.71 | 2046 | 172329.5 | 172391 | 14159 | 1012 |
| mdbook | 2976 | 2.98 | 1036 | 31330.8 | 21955 | 5791 | 8 |

As the ledger judges it (a later run of the day with other work on the machine, 1-minute load 3.6 at its start and 18.6 at its end: mdBook 4,257 ms, oweb 1,277 — the verdict is by the interval's lower end, which is the quiet run's ratio):

| metric | against | ratio | verdict |
|---|---|---:|---|
| `site.scale.build.ms` | mdBook | 3.4× [2.4, 3.4] | ahead |
| `site.scale.page.sent.bytes` | mdBook | 2.2× | ahead |
| `site.scale.written.bytes` | mdBook | 2.0× | ahead |

What the numbers are of:

- **oweb's build was the slowest of the four before it was measured**: 13.2 s, and a page of 88.9 kB with 1,009 links — each page named every route, so a build grew with the square of the pages. A page now names its neighbourhood (the pages in its directory and in each directory above it, every other directory at those levels by its entry; [oweb's design §4.11](../../../../.agents/docs/2026-11-26-oweb-site-framework-design.md)), and a page written as its document alone is built and never laid out: 1.26 ms a page, the same bytes as with a layout.
- **Starlight and VitePress write the whole sidebar into every page** (1,012 and 1,007 links; collapsed groups are still the page's HTML): a page of 172 kB and 401 kB, a site of 172 MB and 410 MB. Starlight's first build of the three took 6.9 s; the next two read its content cache.
- **mdBook keeps its contents in one script** (`toc.js`, filled into each page; a page names 8 links itself, and a reader without scripts gets a page of the contents instead). Its build grows faster than its pages: at 4,000 pages 42.2 s (10.5 ms a page) against oweb's 6.8 s (1.7 ms a page — a page there names 134 links: 80 directories stand beside its own).
- **Growth of oweb's**: 250 pages 0.30 s, 1,000 pages 1.26 s, 4,000 pages 6.8 s.

## What it does not say

- **That a page's navigation offers the same.** From a page of oweb's a reader reaches every page beside it and every directory in one step, any page in two; a sidebar with every page (theirs, with scripts or without) reaches any page in one.
- **That the pages offer the same.** Their default themes carry a sidebar, a search box, a theme switch and their scripts; oweb's page without a program is the document with the site's navigation, the page's contents and the page before and after — its search is the command's and an agent's (`docs.search`, the index as data) until the page takes the program (`oweb-engine`), which has the box. Both of ours are in the table for that reason.
- **Other browsers, other connections.** Chromium only; one profile.
