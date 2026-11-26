# oweb.shell

A page's shell as a view: what stands around a route's document — the site's pages, the page's own sections, the page before and the page after — built by one function from what the site's routes say and from what the page's own HTML says.

Preview (0.1.0 plan ST2; design `.agents/docs/2026-11-26-oweb-site-framework-design.md` §3.3, D-284).

## Why it is a package

The builder (`oweb`) evaluates every route headless and writes it; the program a page loads in a browser draws the same page on a canvas. Both make the page's view here, so a node has one id in the page as it was written and in the page as it is drawn, and the layer over the canvas takes the written elements over one for one. The program carries this package and nothing of the builder (the TypeScript evaluator, the headless host, the tool kernel).

## What it holds

- `Parts`: the shell as data — the pages (`r…`), the sections (`t…`), the page before and after, what each navigation is called, and the document (Markdown, or the page's own HTML).
- `page(parts, layout)`: the view. Ids: `page` the column; in it `nav`, `toc`, `main` holding `doc`, `turn`; around it `view` where the layout scrolls (a canvas is not scrolled by its window).
- `read(projection, document)`: the parts a page's own HTML says, as the Web host hands the page over (`omui.host.web`'s `page::projection`, and the document's markup). Nothing else is sent to a page for its program to draw it: no routes table, no serialized state, no Markdown source.

## Entry points by level

- L0: `oweb::shell::page(parts)`.
- L1: `Layout{.scrolls = true}` for a canvas.
- L2: `read` then `page`: the page a browser has, drawn.
- L3, L4: a shell of one's own is a view of one's own over the same `Parts` (ST2's layouts).

## Agent surface

None of its own: a site is read and built through the tool `oweb`; a page that runs is an application, and its nodes — these — are what an agent reads and operates at `omui.agent`'s endpoint.

## Tests

`oweb`'s `tests/oweb/template_test.cpp`: every page of the minimal documents, read back from its own HTML (with and without its look) and written again, is the HTML that was written.
