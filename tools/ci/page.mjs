// What the site's browser checks share (site-perf.mjs, site-a11y.mjs): a
// built site served as GitHub Pages serves it, and headless Chromium driven
// over the DevTools protocol — no dependencies, node's own fetch and
// WebSocket (node 22, or 20 with --experimental-websocket).
import { spawn } from 'node:child_process';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize } from 'node:path';
import { gzipSync } from 'node:zlib';

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The directory served on a free port of 127.0.0.1 as Pages serves it: gzip
// for what Pages compresses (HTML, scripts, wasm, fonts), the rest as it is.
export async function serve(dir) {
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.ttf': 'font/ttf',
                  '.json': 'application/json', '.md': 'text/markdown', '.webp': 'image/webp', '.png': 'image/png' };
  const gz = new Set(['.html', '.js', '.wasm', '.ttf', '.json', '.md']);
  const zipped = new Map();
  const server = createServer((req, res) => {
    const path = normalize(decodeURIComponent(req.url.split(/[?#]/)[0])).replace(/^(\.\.[/\\])+/, '');
    let file = join(dir, path);
    if (existsSync(file) && statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!existsSync(file)) { res.writeHead(404); res.end(); return; }
    const ext = extname(file), headers = { 'content-type': types[ext] || 'application/octet-stream', 'cache-control': 'max-age=600' };
    let body = readFileSync(file);
    if (gz.has(ext) && /\bgzip\b/.test(req.headers['accept-encoding'] || '')) {
      if (!zipped.has(file)) zipped.set(file, gzipSync(body, { level: 6 }));
      body = zipped.get(file);
      headers['content-encoding'] = 'gzip';
    }
    res.writeHead(200, headers);
    res.end(body);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  return { url: `http://127.0.0.1:${server.address().port}/`, close: () => server.close() };
}

// Chromium, headless, on about:blank: `call` sends a DevTools method, `on`
// hears its events, `value` evaluates an expression (awaited) to its value.
export async function browser(chrome, { width = 1280, height = 800, flags = [] } = {}) {
  const port = 9300 + Math.floor(Math.random() * 500);
  const proc = spawn(chrome, ['--headless=new', '--no-sandbox', '--enable-unsafe-swiftshader', `--remote-debugging-port=${port}`,
                              `--window-size=${width},${height}`, ...flags, 'about:blank'], { stdio: 'ignore' });
  let targets;
  for (let i = 0; i < 50 && !targets; i++) {
    try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); } catch { await sleep(200); }
  }
  if (!targets) throw new Error(`${chrome} did not open its DevTools port`);
  const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r));
  let id = 0;
  const waiting = new Map(), listeners = [];
  ws.addEventListener('message', (m) => {
    const d = JSON.parse(m.data);
    if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d); waiting.delete(d.id); }
    if (d.method) for (const l of listeners) l(d.method, d.params);
  });
  const call = (method, params = {}) => new Promise((r) => { const i = ++id; waiting.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  const value = async (expression) => (await call('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;
  const version = (await (await fetch(`http://127.0.0.1:${port}/json/version`)).json()).Browser;
  await call('Page.enable');
  await call('Runtime.enable');
  await call('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
  return { call, value, version, on: (f) => listeners.push(f), close: () => { ws.close(); proc.kill(); } };
}

// The browser and node to run these with, as tools/ci/browser.sh finds them.
export function usage(script, extra = '') {
  console.error(`usage: CHROME=<chromium> node ${script} (<url> | --serve DIR)${extra}`);
  process.exit(2);
}
