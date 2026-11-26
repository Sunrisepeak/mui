// oweb — a site framework on omui (0.1.0 plan §4.4, D-275): a directory of pages built into a site whose every
// route is one semantic tree written as HTML and as Markdown (`:site`), and the subject `oweb` that offers it to
// people and agents (`:tool`; its catalog oweb.ops.xdef.ts).
//
//     auto built = oweb::build("docs", "dist", {.languages = {"en", "zh"}});
//     // dist/index.html, dist/index.md, dist/guide/start/index.html …, dist/site.routes.xdef.json, dist/llms.txt
export module oweb;

export import :site;
export import :config;
export import :tool;
export import :catalog;
