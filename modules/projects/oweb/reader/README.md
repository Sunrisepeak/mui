# oweb.reader

The program a site's pages load in a browser: it reads the page it finds and draws it. One program for every page of every site that uses oweb's own shell.

Preview (0.1.0 plan ST2; design `.agents/docs/2026-11-26-oweb-site-framework-design.md` §3.3, D-284).

## What it does

A page a site's builder wrote is a document that reads with no script. When the page takes its program — at once, once idle, or when the reader first operates something only a program answers, as the route says — this program starts, asks the page for what it wrote (`omui::shown_document`: every written node's id with its element's label, address and words; the document's markup), and makes from that the view the builder made (`oweb.shell`). The nodes are the same by id, so the layer over the canvas takes the written elements over one for one.

Nothing else is sent for it to draw a page: no routes table, no serialized state, no Markdown source.

## In two parts

Most pages of a site of documents have no live example; what runs one — every base component by name, view documents and their expressions — is wanted only by a page that holds one. The package has two programs linked from the same units: `oweb-reader`, the world's, and `oweb-reader-reading`, the same program that runs no example (`Layout{.examples = nullptr}`), the measure of what every page needs. The world is packed in two parts by what the second carries (`build.mcpp`'s `.part`; `omui-gen dist-toy --part-first`): its functions first, the examples' in `<target>-<hash>.deferred.wasm`, fetched before main only by a page whose document holds a block of the language `view` (`code.language-view`), never by another. Measured (`web` profile, 2026-11-27): the first part 4,648,449 B (1,684,924 gzip), the examples' 1,058,195 B (344,735 gzip), the launcher 208,710 B (53,944 gzip) — a page without an example takes 1.74 MB as sent, one with an example 2.08 MB (the design `.agents/docs/2026-11-27-web-program-size-design.md`).

## Building it

```
omui build --web            # in this directory: a world — target/…/toy/oweb-reader
oweb site.build --site DIR --out DIR --engine <that directory>
```

A world (`omui-gen dist-toy --world`) is a page waiting for its document; the site's builder makes every page from it and serves the program's files once, from `engine/`.

## Entry points by level

- L0: `oweb site.build --engine DIR`: a site whose pages load it.
- L1: a route's `load` (`eager`, `idle`, `interaction`): when its page takes the program.
- L2: the library (`import oweb.reader;`): `view(projection, markup, layout)` — the page as the program draws it; `finder(site)` — the site's search for a page's box, the index fetched at the first words, once; `program(argc, argv, examples)` — the program itself, with what runs a page's live examples or none.
- L3–L4: a shell of one's own is a program of one's own over these and `oweb.shell`'s parts (ST2's layouts).

## Agent surface

An application: at `omui.agent`'s endpoint its nodes are the page's — the navigation's links, the document's blocks — read and operated as any application's.

## Tests

`tests/reader_test.cpp`, without a window: the program run on its own says it has no page; a page read back from what its builder wrote has its parts where the builder put them, and its search asks the site — nothing fetched for a page that is only read, the index once at the first words, what was asked meanwhile answered when it comes; an index that does not come, and a page that names none, find nothing and are not asked for again.

`omui-devtools check takeover --page <a built site> --path /start/`: in a browser, a word of the page moves at most 2 px when the program takes it (UI5's bound); on `/live/` the example's button answers. `omui-devtools site drive <a built site>/start …` reads `window.omuiDeferredState`: `waiting` on a page without an example — its part never fetched — `instantiated` on one with.
