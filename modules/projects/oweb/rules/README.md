# oweb.rules

oweb's build rule. A package that is a site has its site written at each build:

```cpp
// build.mcpp
import mcpp;
import oweb.rules;
int main() { return oweb::rules::site() ? 0 : 1; }
```

```toml
[build-dependencies]
"oweb.rules" = { version = "…", host-module = true }   # quoted: its parent package stands beside it
oweb         = { version = "…", tools = ["oweb"] }       # the command, built for the build: a tool, no library
```

Preview (0.1.0 plan ST4; design `.agents/docs/2026-11-26-oweb-site-framework-design.md` §3.1, §3.5).

- **What it writes:** the site, into `target/site` (`site_options::out`): every route's `index.html` and `index.md`, the routes table, the search index, `llms.txt`, the sitemap, `404.html`, the delivery manifest.
- **How it runs:** as one build action, `oweb site.build --site <the package> --out <out>`, by the command `oweb` built for the build (the site's package names it as a tool of its build; nothing of the builder is linked into the build program). Its inputs are the site's configuration and every file under `pages/`, `public/`, `theme/` and `layouts/`; it runs again when one changed, and the build program runs again when a file is added or taken away.
- **What fails the build:** what the command refuses — a route over its budget, an example that cannot run, a route whose page is not there — said as the command says it.
- **Options:** `site` (the site's directory where it is not the package's own), `out`, `engine` (a world's directory: the program the pages load in a browser; empty: what the site's configuration says).

## Why it is a package

It is compiled into a site's build program on the build machine (`host-module = true`) and depends on none of the builder: the builder (`oweb`) is a library and a command for any machine, taken by a site's build as a tool.

## Agent surface

None of its own: a site is read and built through the subject `oweb` (the command this rule runs); an agent that builds a site's package (`mcpp build`) gets the same site.

## Tests

`omui-devtools check templates`: the `docs` template rendered as `mcpp new` renders it, pointed at this checkout and built — its site is in `target/site`, each route's two files there.
