# oweb.shell

A page's shell as a view: what stands around a route's document — the site's pages, the page's own sections, the page before and the page after — built by one function from what the site's routes say and from what the page's own HTML says.

Preview (0.1.0 plan ST2; design `.agents/docs/2026-11-26-oweb-site-framework-design.md` §3.3, D-284).

## Why it is a package

The builder (`oweb`) evaluates every route headless and writes it; the program a page loads in a browser draws the same page on a canvas. Both make the page's view here, so a node has one id in the page as it was written and in the page as it is drawn, and the layer over the canvas takes the written elements over one for one. The program carries this package and nothing of the builder (the TypeScript evaluator, the headless host, the tool kernel).

## What it holds

- `Parts`: the shell as data — the pages (`r…`), the sections (`t…`), the page before and after, what each navigation is called, and the document (Markdown, or the page's own HTML).
- `page(parts, layout)`: the view, for where an application's view is mounted (`main`). Ids there: `main/page` the column; in it `nav`, `toc`, `main` holding `doc`, `turn` — the same whether the window scrolls the page or a view of the window's size does (`Layout::scrolls`: a canvas is not scrolled by its window).
- `read(projection, markup)`: the parts a page's own HTML says, as the Web host hands the page over (`omui.host.web`'s `page::projection`, and the document's markup, asked by its node). Nothing else is sent to a page for its program to draw it: no routes table, no serialized state, no Markdown source. A part is known by its own id wherever a view placed it — `nav`, `toc`, `turn` each a navigation whose links (at any depth under it) are the part's, `doc` the document — so the parts are read back from a page of any view.
- The site's search: the index's writer and reader (`search_json`, `sections_from`) and the query (`search`) — here so that the builder's `docs.search` and a page's box answer by the same function. `Parts::search` names the box (the page's language); `search_box(label, find)` is the part (`search`: a text box `box` and its `results` — a link per section found, a line under it); `Layout::find` is who answers (`Finder`: asked at each change, it may answer later — an index is fetched on first use — and an answer for words no longer in the box is dropped); `found_in(sections, query, top)` is the answer from an index.
- `Layout::examples`: who runs a page's live examples (a block of the language `view`), called once — when the first page that holds one is made (`holds_examples`): the shell's own by default (`install_examples`: every base component by name, omui.ui.markdown's view documents). A program that never runs one says `.examples = nullptr` and carries none of it; an example is then shown as its source.
- `View`, and what `page` is made of — `navigation`, `links`, `document`, `view`: a site's own view of the same parts (the pages at the side, the contents beside the document, a part left out). The builder is given it (`oweb::SiteOptions::view`, `oweb::Sites{view}`) and so is the program its pages load; a page as written is then the page as drawn whatever its view.

## Entry points by level

- L0: `oweb::shell::page(parts)`.
- L1: `Layout{.scrolls = true}` for a canvas; `Layout{.find = …}` for a page with a search box; `Layout{.follow = …}` for who follows a link pressed in the document (the program's runtime: `Runtime::open_url`, which on a page follows the site's own addresses in this tab).
- L2: `read` then `page`: the page a browser has, drawn.
- L3: a view of one's own from the shell's pieces — `view({row({.id = "page"}, {column({.id = "side"}, {navigation(column({.id = "nav"}, links(parts.pages)), parts.site)}), document(parts)})}, layout)`.
- L4: any `View`: a function from `Parts` to an element that keeps the parts' names.

## Agent surface

None of its own: a site is read and built through the tool `oweb`; a page that runs is an application, and its nodes — these — are what an agent reads and operates at `omui.agent`'s endpoint.

## Tests

`oweb`'s `tests/site/template_test.cpp`: every page of the minimal documents, read back from its own HTML (with and without its look) and written again, is the HTML that was written; with a navigation of its own (the pages at the side, the contents under them, no turn) every route says the same document — its Markdown the same bytes, its HTML the same but for where it stands — reads back to the same parts and is written again as it was, and the site builds.
