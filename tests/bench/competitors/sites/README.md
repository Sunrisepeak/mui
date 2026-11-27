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

## What it does not say

- **How a build grows with the site.** Eight routes are a build's start: every side's time here is mostly its own start-up (Node and Vite; a process and its fonts). A corpus of a thousand pages is the next measurement.
- **That the pages offer the same.** Their default themes carry a sidebar, a search box, a theme switch and their scripts; oweb's page without a program is the document with the site's navigation, the page's contents and the page before and after — its search is the command's and an agent's (`docs.search`, the index as data) until the page takes the program (`oweb-engine`), which has the box. Both of ours are in the table for that reason.
- **Other browsers, other connections.** Chromium only; one profile.
