// The site's delivery measured in a real browser (headless Chromium over
// the DevTools protocol, real time): what the page transfers (each response's
// encoded bytes, by kind), how long until the program draws its first frame
// (its semantic mirror filled after it) and what had arrived by then (the
// rest is fetched after it: the fighters' pictures), until it is ready (window.omui
// answers), and its frame tails — the intervals between animation frames
// while it flies to a place and back, p50 / p95 / p99 / max and the frames
// longer than 33 ms — with the heap's and the wasm memory's size at the end.
// A cold visit (a fresh profile), over a connection when --network says one
// (MBIT/RTT_MS). Prints a JSON report; with --budget FILE
// (site/delivery.budget.omui.json) the exit code says whether every figure
// is within its budget.
//
//   CHROME=<chromium> node tools/ci/site-perf.mjs (<url> | --serve DIR) [--width W] [--height H] [--network 20/40] [--budget FILE]
//       [--cache enabled|disabled] [--request-log FILE]
// A fresh profile still permits HTTP cache reuse within one visit by default.
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { browser, serve, sleep, usage } from './page.mjs';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const w = +opt('width', 1280), h = +opt('height', 800), budgetFile = opt('budget', ''), dir = opt('serve', ''), network = opt('network', '');
const chrome = process.env.CHROME;
const networkParts = typeof network === 'string' ? network.split('/') : [];
const networkValues = networkParts.map(Number);
if (args.includes('--network') && (networkParts.length !== 2 || networkParts.some(v => !v.trim()) ||
    !networkValues.every(Number.isFinite) || networkValues[0] <= 0 || networkValues[1] < 0))
  usage('site-perf.mjs', ' --network MBIT/RTT_MS (finite MBIT > 0 and RTT_MS >= 0)');
const cache = opt('cache', 'enabled');
if (!['enabled','disabled'].includes(cache)) usage('site-perf.mjs', ' --cache enabled|disabled');
let url = args[0] && !args[0].startsWith('--') ? args[0] : '';
if (!chrome || (!url && !dir)) usage('site-perf.mjs', ' [--width W] [--height H] [--network MBIT/RTT] [--budget FILE]');
const server = dir ? await serve(dir) : null;
if (server) url = server.url;

const profile = mkdtempSync(join(tmpdir(), 'omui-site-perf-'));
const b = await browser(chrome, { width: w, height: h, flags:['--user-data-dir='+profile] });
const responses = new Map(), bytes = {}, errors = [], requestLog = [], byId = new Map();
b.on((method, p) => {
  if (method === 'Network.requestWillBeSent') {
    const row = {id:p.requestId,path:new URL(p.request.url).pathname,kind:p.type,initiator:p.initiator?.type,encoded_body_event_bytes:0,finished_bytes:[]};
    requestLog.push(row); byId.set(p.requestId,row);
  }
  const row = byId.get(p.requestId);
  if (row && method === 'Network.responseReceived') Object.assign(row,{status:p.response.status,cache:!!row.cache||!!p.response.fromDiskCache||!!p.response.fromPrefetchCache,service_worker:!!p.response.fromServiceWorker});
  if (row && method === 'Network.requestServedFromCache') row.cache = true;
  if (row && method === 'Network.dataReceived') row.encoded_body_event_bytes += p.encodedDataLength;
  if (row && method === 'Network.loadingFinished') row.finished_bytes.push(p.encodedDataLength);
  if (method === 'Network.responseReceived') responses.set(p.requestId, p.type);
  if (method === 'Network.loadingFinished') {
    const kind = (responses.get(p.requestId) || 'Other').toLowerCase();
    bytes[kind] = (bytes[kind] || 0) + p.encodedDataLength;
  }
  if (method === 'Runtime.exceptionThrown') errors.push(JSON.stringify(p.exceptionDetails).slice(0, 300));
});
await b.call('Network.enable');
await b.call('Network.setCacheDisabled',{cacheDisabled:cache==='disabled'});
await b.call('Network.setBypassServiceWorker',{bypass:true});
if (network) {
  const [mbit, rtt] = networkValues;
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
let first = null, ready = null, before_first = null;
const sum = () => Object.values(bytes).reduce((x, y) => x + y, 0);
for (let i = 0; i < 300 && (first === null || ready === null); i++) {
  await sleep(100);
  const p = await b.value('window.__perf');
  if (p) ({ first, ready } = p);
  if (first !== null && before_first === null) before_first = sum();  // what had arrived when it first drew (to within 100 ms)
}
await sleep(1500);  // its fonts arrived, its opening at rest
// The frame tails: animation-frame intervals while it flies to a place and back.
const tails = await b.value(`new Promise(async (done) => {
  const gaps = []; let last = null, running = true;
  const tick = (t) => { if (last !== null) gaps.push(t - last); last = t; if (running) requestAnimationFrame(tick); };
  requestAnimationFrame(tick);
  const acted = [];
  const act = async (id) => { const r = window.omui ? await window.omui.act(id, 'activate') : null; acted.push({ node: id, exitCode: r ? r.exitCode : null }); };
  await act('main/bar/nav/to_docs'); await new Promise((r) => setTimeout(r, 250));
  await act('main/body/view/docs/page/directory/to_features'); await new Promise((r) => setTimeout(r, 2500));
  await act('main/bar/mark'); await new Promise((r) => setTimeout(r, 2500));
  running = false;
  gaps.sort((a, b) => a - b);
  const q = (p) => gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(p * gaps.length))] : null;
  done({ acted, frames: gaps.length, p50: q(0.5), p95: q(0.95), p99: q(0.99), max: gaps.length ? gaps[gaps.length - 1] : null,
         long: gaps.filter((g) => g > 33.4).length });
})`);
const memory = await b.value(`({ heap: performance.memory ? performance.memory.usedJSHeapSize : null,
  wasm: typeof HEAP8 !== 'undefined' ? HEAP8.length : null })`);
const total = Object.values(bytes).reduce((x, y) => x + y, 0);
const report = { url, size: [w, h], network: network || null, cache, browser: b.version,
  transfer: { total, before_first_frame: before_first, by_kind: bytes }, first_frame_ms: first, ready_ms: ready, frames: tails, memory, errors };
if (opt('request-log','')) writeFileSync(opt('request-log',''),JSON.stringify(requestLog,null,2));
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
// Chrome may still be writing its profile as it exits: a leftover temporary
// directory is not a result, so cleaning up never decides the verdict.
try { rmSync(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }); } catch {}
if (server) server.close();
process.exit(ok ? 0 : 1);
