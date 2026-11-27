# oweb.reader — skill

How an Agent inside a page of a site asks the site — the subject `site` the page's program adds to its runtime. The operations and their parameters are the catalog's ([`site.ops.xdef.ts`](site.ops.xdef.ts)); this says the order to use them in. Outside a page the same questions are the `oweb` command's ([its skill](../skill.omui.md)) or plain fetches of the site's files.

## Before

1. The page has taken its program (a route that loads it `eager` has at once; one that waits takes it at the first thing only a program answers). The subject is asked where the page's interface is asked, by its operations' whole names: in the page, `window.omui.request({op: "site.describe"})`, or the tools the page offers a browser's agent (WebMCP: `site_describe`, `routes_list`, `page_read`, `docs_search`); connected by `omui connect <the page's code>`, `omui page call '{"op":"site.describe"}'`. `{"op":"subjects"}` says who is there.
2. Nothing of the site is fetched until something is asked. The first asking fetches the table it needs (the routes, or the search's index), once.

## Reading

1. Not knowing the site: `site.describe` — its title, address, languages, each route with its title and its Markdown's address, and the requests to go on with, sent as they stand. Its `text` is the site's `llms.txt`, word for word.
2. A question: `docs.search` with `query` — the sections that hold every word, best first: route, anchor, the headings above, the line that says so. The words are put in the page's own search box, and what they find is listed under it: whoever reads the page sees what was asked.
3. A page: `page.read` with `route` (`/guide/start/`) — its Markdown, the file the site serves at that route's `index.md`. Read this, not the page's nodes, for what a page says; `as: "html"` gives the page as written.
4. `routes.list` — every route with its headings and their anchors.

## Verifying

- An answer is the one `oweb <operation> --site <the built site>` gives: compare `text` of `site.describe` with the site's `llms.txt`, a `page.read` with the route's `index.md`.
- A file that did not come is said with its address and why; ask again (it is fetched again), or ask `site.describe` for what the site holds.

## Not here

Operating the page — following a link, pressing a button of a live example — is the interface's (`ui.snapshot`, `ui.act`). Building a site is the `oweb` command's.
