# oweb — skill

How an Agent (or a person at a terminal) works with a site built on omui without opening a page: reads it, checks what it will publish, builds it. The operations and their parameters are the catalog's ([`oweb.ops.xdef.ts`](oweb.ops.xdef.ts)); this says the order to use them in.

## Before

1. A site is a directory with `pages/`: each Markdown file is a route. `routes.list --site DIR` says the routes: address, source, title, language, headings with their anchors.
2. A site states what it is in `oweb.config.ts` (`defineSite` from `oweb/define`: `title`, `url`, `languages`, `routes`, `budgets`); every operation reads it, and an option of the request stands over it. A site with more than one language names them, the default first (`languages: ["en", "zh"]`, or `--languages en,zh`): the pages under `pages/zh/` are then the Chinese ones. Stated `routes` are the site's pages and their order; without them every page of `pages/` is a route. A stated route with `reference: "<product>.ops.xdef.ts"` instead of `source` is that catalog's reference, generated at each build; link to it by its address (`/reference/ops/`). A stated route with `page: "<directory>"` is an application's page as its build wrote it (`omui build --web`): served as built with its files, and listed, searched and read as Markdown by what its own document says; link to it by its address (`/`), and from it by addresses from where it is served (`start/`).

## Reading

1. A question first: `docs.search --site DIR --query "words"` answers with the sections that hold every word — route, anchor, the headings above, the line that says so, and where the page's Markdown is. Then read that page.
2. `page.read --site DIR --route /guide/start/` gives the page as Markdown: the same text the built site serves at `/guide/start/index.md`. Read this, not the HTML.
3. `--as html` gives the whole page as it reads with no script.
4. On a published site there is no command: fetch `llms.txt` (every page's title and its Markdown's address), then the `index.md` of the page; `site.routes.xdef.json` has the headings and anchors, `site.search.xdef.json` every section's text.

## Building

1. `site.build --site DIR --out OUT` writes every route's `index.html` and `index.md`, the pictures beside the pages, `site.routes.xdef.json`, `llms.txt`, `site.search.xdef.json` and `site.delivery.xdef.json`; `404.html` (the site's `pages/404.md`, or a default) and `.nojekyll`; with `--url https://…/` also `sitemap.xml`, and `404.html`'s links are from that address's path. The answer's `budgets` say what each route sends before it reads; over the budget the build fails (exit 1) naming the route — shorten the page or state the budget with `--readable BYTES`.
2. The answer lists each file with its bytes. Undo: delete `OUT`.

## Verifying

- A link between pages is written `guide/start.md` in the source: check it became the route's directory in `index.html` and its `index.md` in the Markdown.
- Two pages of one address (`a.md` and `a/index.md`) are refused, naming both.
- Record the command and the answer's `files`.

## Not here

Writing an application is `omui`'s; the engine on the page, layouts, search and deployment come with the framework's later tasks (README).
