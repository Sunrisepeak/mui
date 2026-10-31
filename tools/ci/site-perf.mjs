// The site's delivery measured in a real browser (headless Chromium over
// the DevTools protocol, real time): what the page transfers (each response's
// encoded bytes, by kind), how long until the program draws its first frame
// and until it is ready (window.omui answers), and its frame tails — the
// intervals between animation frames while it flies to a place and back,
// p50 / p95 / p99 / max and the frames longer than 33 ms — with the heap's
// and the wasm memory's size at the end. Prints a JSON report; with --budget
// FILE (site.budget.omui.json) the exit code says whether every figure is
// within its budget.
//
//   CHROME=<chromium> node tools/ci/site-perf.mjs <url> [--width W] [--height H] [--budget FILE]
import { spawn } from 'node:child_process';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { createServer } from 'node:http';
import { extname, join, normalize } from 'node:path';
import { gzipSync } from 'node:zlib';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const w = +opt('width', 1280), h = +opt('height', 800), budgetFile = opt('budget', ''), dir = opt('serve', '');
const network = opt('network', '');  // MBIT/RTT_MS: a connection's throughput and round trip, e.g. 20/40
const chrome = process.env.CHROME;
let url = args[0] && !args[0].startsWith('--') ? args[0] : '';
if (!chrome || (!url && !dir)) {
  console.error('usage: CHROME=<chromium> node site-perf.mjs (<url> | --serve DIR) [--width W] [--height H] [--budget FILE]');
  process.exit(2);
}
// --serve DIR: the built site served as GitHub Pages serves it — gzip for
// what Pages compresses (HTML, scripts, wasm, fonts), as it is otherwise.
let server = null;
if (dir) {
  const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.ttf': 'font/ttf',
                  '.json': 'application/json', '.md': 'text/markdown', '.webp': 'image/webp', '.png': 'image/png' };
  const gz = new Set(['.html', '.js', '.wasm', '.ttf', '.json', '.md']);
  const zipped = new Map();
  server = createServer((req, res) => {
    let path = normalize(decodeURIComponent(req.url.split(/[?#]/)[0])).replace(/^(\.\.[/\\])+/, '');
    let file = join(dir, path);
    if (existsSync(file) && statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!existsSync(file)) { res.writeHead(404); res.end(); return; }
    const ext = extname(file), headers = { 'content-type': types[ext] || 'application/octet-stream', 'cache-control': 'max-age=600' };
    let body = readFileSync(file);
    if (gz.has(ext) && /\bgzip\b/.test(req.headers['accept-encoding'] || '')) {
      if (!zipped.has(file)) zipped.set(file, gzipSync(body, { level: 6 }));
      body = zipped.get(file); headers['content-encoding'] = 'gzip';
    }
    res.writeHead(200, headers); res.end(body);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  url = `http://127.0.0.1:${server.address().port}/`;
}
const port = 9300 + Math.floor(Math.random() * 500);
const proc = spawn(chrome, ['--headless=new', '--no-sandbox', '--enable-unsafe-swiftshader', `--remote-debugging-port=${port}`, `--window-size=${w},${h}`, 'about:blank'],
  { stdio: 'ignore' });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch { await sleep(200); } }
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r));
let id = 0; const waiting = new Map(); const responses = new Map(); const bytes = {}; const errors = [];
ws.addEventListener('message', (m) => {
  const d = JSON.parse(m.data);
  if (d.id && waiting.has(d.id)) { waiting.get(d.id)(d); waiting.delete(d.id); }
  if (d.method === 'Network.responseReceived') responses.set(d.params.requestId, d.params.type);
  if (d.method === 'Network.loadingFinished') {
    const kind = (responses.get(d.params.requestId) || 'Other').toLowerCase();
    bytes[kind] = (bytes[kind] || 0) + d.params.encodedDataLength;
  }
  if (d.method === 'Runtime.exceptionThrown') errors.push(JSON.stringify(d.params.exceptionDetails).slice(0, 300));
});
const call = (method, params = {}) => new Promise((r) => { const i = ++id; waiting.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
const value = async (expression) => (await call('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result?.result?.value;
await call('Page.enable');
await call('Runtime.enable');
await call('Network.enable');
if (network) {
  const [mbit, rtt] = network.split('/').map(Number);
  await call('Network.emulateNetworkConditions', { offline: false, latency: rtt, downloadThroughput: mbit * 125000, uploadThroughput: mbit * 125000 / 4 });
}
await call('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
// From navigation: the first frame the program draws on its canvas, and when it answers as an agent.
await call('Page.addScriptToEvaluateOnNewDocument', { source: `
  window.__perf = { first: null, ready: null };
  (function watch() {
    const c = document.getElementById('canvas');
    const m = document.querySelector('[data-omui=mirror]');
    if (m && m.childElementCount > 0 && window.__perf.first === null) window.__perf.first = performance.now();
    if (window.omui && window.__perf.ready === null) window.__perf.ready = performance.now();
    if (window.__perf.first === null || window.__perf.ready === null) requestAnimationFrame(watch);
  })();` });
await call('Page.navigate', { url });
let first = null, ready = null;
for (let i = 0; i < 300 && (first === null || ready === null); i++) {
  await sleep(100);
  const p = await value('window.__perf');
  if (p) ({ first, ready } = p);
}
await sleep(1500);  // its fonts arrived, its opening at rest
// The frame tails: animation-frame intervals while it flies to a place and back.
const tails = await value(`new Promise(async (done) => {
  const gaps = []; let last = null, running = true;
  const tick = (t) => { if (last !== null) gaps.push(t - last); last = t; if (running) requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  const acted = [];
  const act = async (id) => { const r = window.omui ? await window.omui.act(id, 'activate') : null; acted.push({ node: id, exitCode: r ? r.exitCode : null }); };
  await act('main/body/nav/to_features'); await new Promise((r) => setTimeout(r, 2500));
  await act('main/body/nav/to_home'); await new Promise((r) => setTimeout(r, 2500));
  running = false;
  gaps.sort((a, b) => a - b);
  const q = (p) => gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(p * gaps.length))] : null;
  done({ acted, frames: gaps.length, p50: q(0.5), p95: q(0.95), p99: q(0.99), max: gaps.length ? gaps[gaps.length - 1] : null,
         long: gaps.filter((g) => g > 33.4).length });
})`);
const memory = await value(`({ heap: performance.memory ? performance.memory.usedJSHeapSize : null,
  wasm: typeof HEAP8 !== 'undefined' ? HEAP8.length : null })`);
const total = Object.values(bytes).reduce((a, b) => a + b, 0);
const report = { url, size: [w, h], network: network || null, browser: (await (await fetch(`http://127.0.0.1:${port}/json/version`)).json()).Browser,
  transfer: { total, by_kind: bytes }, first_frame_ms: first, ready_ms: ready, frames: tails, memory, errors };
let ok = true;
if (budgetFile) {
  const budget = JSON.parse(readFileSync(budgetFile, 'utf8')).budgets;
  report.budgets = [];
  const at = (path) => path.split('.').reduce((o, k) => (o == null ? o : o[k]), report);
  for (const b of budget) {
    const v = at(b.metric);
    const pass = typeof v === 'number' && v <= b.max;
    ok = ok && pass;
    report.budgets.push({ metric: b.metric, value: v, max: b.max, pass });
  }
}
console.log(JSON.stringify(report, null, 2));
ws.close(); proc.kill(); if (server) server.close();
process.exit(ok ? 0 : 1);
