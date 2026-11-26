# oweb — skill

How an Agent (or a person at a terminal) works with a site built on omui without opening a page: reads it, checks what it will publish, builds it. The operations and their parameters are the catalog's ([`oweb.ops.xdef.ts`](oweb.ops.xdef.ts)); this says the order to use them in.

## Before

1. A site is a directory with `pages/`: each Markdown file is a route. `routes.list --site DIR` says the routes: address, source, title, language, headings with their anchors.
2. A site with more than one language names them, the default first: `--languages en,zh`. The pages under `pages/zh/` are then the Chinese ones.

## Reading

1. `page.read --site DIR --route /guide/start/` gives the page as Markdown: the same text the built site serves at `/guide/start/index.md`. Read this, not the HTML.
2. `--as html` gives the whole page as it reads with no script.
3. On a published site there is no command: fetch `llms.txt` (every page's title and its Markdown's address), then the `index.md` of the page; `routes.xdef.json` has the headings and anchors.

## Building

1. `site.build --site DIR --out OUT` writes every route's `index.html` and `index.md`, the pictures beside the pages, `routes.xdef.json` and `llms.txt`; with `--url https://…/` also `sitemap.xml`.
2. The answer lists each file with its bytes. Undo: delete `OUT`.

## Verifying

- A link between pages is written `guide/start.md` in the source: check it became the route's directory in `index.html` and its `index.md` in the Markdown.
- Two pages of one address (`a.md` and `a/index.md`) are refused, naming both.
- Record the command and the answer's `files`.

## Not here

Writing an application is `omui`'s; the engine on the page, layouts, search and deployment come with the framework's later tasks (README).
