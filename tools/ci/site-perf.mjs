// The site's delivery measured in a real browser (headless Chromium over
// the DevTools protocol, real time): what the page transfers (each response's
// encoded bytes, by kind), how long until the program draws its first frame
// (its semantic mirror filled after it) and until it is ready (window.omui
// answers), and its frame tails — the intervals between animation frames
// while it flies to a place and back, p50 / p95 / p99 / max and the frames
// longer than 33 ms — with the heap's and the wasm memory's size at the end.
// A cold visit (a fresh profile), over a connection when --network says one
// (MBIT/RTT_MS). Prints a JSON report; with --budget FILE
// (site/delivery.budget.omui.json) the exit code says whether every figure
// is within its budget.
//
//   CHROME=<chromium> node tools/ci/site-perf.mjs (<url> | --serve DIR) [--width W] [--height H] [--network 20/40] [--budget FILE]
import { readFileSync } from 'node:fs';
import { browser, serve, sleep, usage } from './page.mjs';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const w = +opt('width', 1280), h = +opt('height', 800), budgetFile = opt('budget', ''), dir = opt('serve', ''), network = opt('network', '');
const chrome = process.env.CHROME;
let url = args[0] && !args[0].startsWith('--') ? args[0] : '';
if (!chrome || (!url && !dir)) usage('site-perf.mjs', ' [--width W] [--height H] [--network MBIT/RTT] [--budget FILE]');
const server = dir ? await serve(dir) : null;
if (server) url = server.url;

const b = await browser(chrome, { width: w, height: h });
const responses = new Map(), bytes = {}, errors = [];
b.on((method, p) => {
  if (method === 'Network.responseReceived') responses.set(p.requestId, p.type);
  if (method === 'Network.loadingFinished') {
    const kind = (responses.get(p.requestId) || 'Other').toLowerCase();
    bytes[kind] = (bytes[kind] || 0) + p.encodedDataLength;
  }
  if (method === 'Runtime.exceptionThrown') errors.push(JSON.stringify(p.exceptionDetails).slice(0, 300));
});
await b.call('Network.enable');
if (network) {
  const [mbit, rtt] = network.split('/').map(Number);
  await b.call('Network.emulateNetworkConditions', { offline: false, latency: rtt, downloadThroughput: mbit * 125000, uploadThroughput: mbit * 125000 / 4 });
}
// From navigation: the first frame the program has drawn (its semantic mirror filled after it), and when it answers as an agent.
await b.call('Page.addScriptToEvaluateOnNewDocument', { source: `
  window.__perf = { first: null, ready: null };
  (function watch() {
    const m = document.querySelector('[data-omui=mirror]');
    if (m && m.childElementCount > 0 && window.__perf.first === null) window.__perf.first = performance.now();
    if (window.omui && window.__perf.ready === null) window.__perf.ready = performance.now();
    if (window.__perf.first === null || window.__perf.ready === null) requestAnimationFrame(watch);
  })();` });
await b.call('Page.navigate', { url });
let first = null, ready = null;
for (let i = 0; i < 300 && (first === null || ready === null); i++) {
  await sleep(100);
  const p = await b.value('window.__perf');
  if (p) ({ first, ready } = p);
}
await sleep(1500);  // its fonts arrived, its opening at rest
// The frame tails: animation-frame intervals while it flies to a place and back.
const tails = await b.value(`new Promise(async (done) => {
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
const memory = await b.value(`({ heap: performance.memory ? performance.memory.usedJSHeapSize : null,
  wasm: typeof HEAP8 !== 'undefined' ? HEAP8.length : null })`);
const total = Object.values(bytes).reduce((x, y) => x + y, 0);
const report = { url, size: [w, h], network: network || null, browser: b.version,
  transfer: { total, by_kind: bytes }, first_frame_ms: first, ready_ms: ready, frames: tails, memory, errors };
let ok = true;
if (budgetFile) {
  report.budgets = [];
  const at = (path) => path.split('.').reduce((o, k) => (o == null ? o : o[k]), report);
  for (const x of JSON.parse(readFileSync(budgetFile, 'utf8')).budgets) {
    const v = at(x.metric);
    const pass = typeof v === 'number' && v <= x.max;
    ok = ok && pass;
    report.budgets.push({ metric: x.metric, value: v, max: x.max, pass });
  }
}
console.log(JSON.stringify(report, null, 2));
b.close();
if (server) server.close();
process.exit(ok ? 0 : 1);
