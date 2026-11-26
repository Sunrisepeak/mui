# oweb.reader

The program a site's pages load in a browser: it reads the page it finds and draws it. One program for every page of every site that uses oweb's own shell.

Preview (0.1.0 plan ST2; design `.agents/docs/2026-11-26-oweb-site-framework-design.md` §3.3, D-284).

## What it does

A page a site's builder wrote is a document that reads with no script. When the page takes its program — at once, once idle, or when the reader first operates something only a program answers, as the route says — this program starts, asks the page for what it wrote (`omui::shown_document`: every written node's id with its element's label, address and words; the document's markup), and makes from that the view the builder made (`oweb.shell`). The nodes are the same by id, so the layer over the canvas takes the written elements over one for one.

Nothing else is sent for it to draw a page: no routes table, no serialized state, no Markdown source, no Markdown parser.

## Building it

```
omui build --web            # in this directory: a world — target/…/toy/oweb-reader
oweb site.build --site DIR --out DIR --engine <that directory>
```

A world (`omui-gen dist-toy --world`) is a page waiting for its document; the site's builder makes every page from it and serves the program's files once, from `engine/`.

## Entry points by level

- L0: `oweb site.build --engine DIR`: a site whose pages load it.
- L1: a route's `load` (`eager`, `idle`, `interaction`): when its page takes the program.
- L2–L4: a shell of one's own is a program of one's own over `oweb.shell`'s parts (ST2's layouts).

## Agent surface

An application: at `omui.agent`'s endpoint its nodes are the page's — the navigation's links, the document's blocks — read and operated as any application's.

## Tests

`omui-devtools check takeover --page <a built site> --path /start/`: in a browser, a word of the page moves at most 2 px when the program takes it (UI5's bound).
