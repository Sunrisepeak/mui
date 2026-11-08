// md4c's own HTML writer (md4w: md4c as WebAssembly) and markdown-it, on the
// corpus omui-bench-markdown reads (src/markdown.cpp), in the same node: the
// competitors' side of Markdown to HTML.
//
//   node markdown.mjs CORPUS [--runs N] [--json]   one JSON line a competitor
import { readFileSync } from 'node:fs';
import { init, mdToHtml } from 'md4w';
import MarkdownIt from 'markdown-it';

const args = process.argv.slice(2);
const text = readFileSync(args[0], 'utf8');
const runs = args.includes('--runs') ? Number(args[args.indexOf('--runs') + 1]) : 15;
const bytes = Buffer.byteLength(text);
const timed = (name, render) => {
  let html = '';
  const times = [];
  for (let run = 0; run < 3 + runs; ++run) {
    const begin = process.hrtime.bigint();
    html = render(text);
    const spent = Number(process.hrtime.bigint() - begin) / 1e6;
    if (run >= 3) times.push(spent);
  }
  times.sort((a, b) => a - b);
  const median = times[times.length >> 1];
  console.log(JSON.stringify({ name, bytes, html_bytes: Buffer.byteLength(html), median_ms: +median.toFixed(3), mb_per_s: +(bytes / 1048576 / (median / 1000)).toFixed(2) }));
};
await init();
// Our gfm's flags exactly (mlibs.markdown: tables, strikethrough, task lists,
// permissive URL/www/e-mail autolinks); md4w also writes an anchor into every
// heading, a little more output than ours.
timed('md4w', (source) => mdToHtml(source, { parseFlags: ['TABLES', 'STRIKETHROUGH', 'TASKLISTS', 'PERMISSIVE_URL_AUTO_LINKS', 'PERMISSIVE_WWW_AUTO_LINKS', 'PERMISSIVE_EMAIL_AUTO_LINKS'] }));
const it = new MarkdownIt({ linkify: true });
timed('markdown-it', (source) => it.render(source));
