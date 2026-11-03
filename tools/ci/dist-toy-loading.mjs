// Execute the actual generated inline loader against native fetch/Response/
// ReadableStream/WebAssembly. The launcher lifecycle and MEMFS are small
// test doubles; this does not claim to execute a complete Emscripten app.
// First run gen.dist_toy, then: node tools/ci/dist-toy-loading.mjs <index.html>
import { readFile } from 'node:fs/promises';
import { dirname, join, extname } from 'node:path';
import { createServer } from 'node:http';
import { gzipSync } from 'node:zlib';
import vm from 'node:vm';

const fixture = process.argv[2] || '/tmp/openmui-tests/dist-toy-loading/index.html';
const html = await readFile(fixture, 'utf8');
const loaderSource = html => [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].map(m => m[1]).find(s => s.includes('var omuiWasm =') && s.includes('window.omuiLoad ='));
const source = loaderSource(html);
if (!source) throw new Error('Fixture lacks the managed loader; run the current gen.dist_toy tests first');
const report = { fixture, checks: [] };
function check(ok, what, detail) { report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : { detail }) }); if (!ok) throw new Error(what); }
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, what) {
  const end = Date.now() + 4000;
  while (Date.now() < end) { if (await fn()) return; await sleep(5); }
  throw new Error('Timed out: ' + what);
}
const requests = [];
const server = createServer(async (req, res) => {
  try {
    const name = decodeURIComponent(req.url.split('?')[0]).replace(/^\//, '');
    if (name.includes('..')) { res.writeHead(400); res.end(); return; }
    const bytes = await readFile(join(dirname(fixture), name)), encoded = gzipSync(bytes);
    requests.push({ name, raw: bytes.length, encoded: encoded.length });
    res.writeHead(200, { 'Content-Type': extname(name) === '.wasm' ? 'application/wasm' : 'application/octet-stream', 'Content-Encoding': 'gzip', 'Content-Length': encoded.length });
    res.end(encoded);
  } catch { res.writeHead(404); res.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const base = `http://127.0.0.1:${server.address().port}/`;
const instances = [];

function harness(options = {}) {
  const assetBase = new URL(options.prefix || '', base);
  const events = [], listeners = new Map(), files = new Map(), aliases = new Map(), dependencies = new Set(), calls = [];
  const timers = new Map(); let serial = 0, receives = 0, initialized = 0, streamed = 0, buffered = 0;
  const status = { textContent: '', style: {} }, canvas = { focus() {}, addEventListener() {} };
  const behavior = { fail: null, status: 404, interrupt: false, hang: false, holdWasm: false, releaseWasm: null };
  const env = {
    console, URL, Headers, Response, ReadableStream: options.noStream ? undefined : ReadableStream,
    AbortController, Uint8Array, setTimeout(fn, ms) {
      const id = ++serial; const entry = { fn, ms }; timers.set(id, entry);
      // Backoff is accelerated; inactivity and first-frame timers are fired
      // explicitly by tests, rather than waiting 15/60 seconds or 4 seconds.
      if (ms < 4000) entry.handle = setTimeout(() => { timers.delete(id); fn(); }, 0);
      return id;
    },
    clearTimeout(id) { const t = timers.get(id); if (t?.handle) clearTimeout(t.handle); timers.delete(id); },
    CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init.detail; } },
    document: { URL: assetBase.href + '#/' + (options.language || 'en'), getElementById: id => id === 'canvas' ? canvas : status },
    navigator: { languages: [options.browserLanguage || 'en'], language: options.browserLanguage || 'en' }, location: { reload() { throw new Error('Unexpected reload'); } },
    addEventListener(name, fn) { if (!listeners.has(name)) listeners.set(name, []); listeners.get(name).push(fn); },
    dispatchEvent(event) { if (event.type === 'omui-load') events.push(event.detail); for (const fn of listeners.get(event.type) || []) fn(event); return true; },
    FS: {
      mkdirTree() {},
      writeFile(path, bytes) { files.set(path, typeof bytes === 'string' ? new TextEncoder().encode(bytes) : new Uint8Array(bytes)); },
      unlink(path) { if (!files.delete(path) && !aliases.delete(path)) throw new Error('No such file: ' + path); },
      symlink(target, path) { if (aliases.has(path)) throw new Error('Duplicate alias'); aliases.set(path, target); }
    },
    WebAssembly: {
      CompileError: WebAssembly.CompileError, LinkError: WebAssembly.LinkError, RuntimeError: WebAssembly.RuntimeError,
      instantiateStreaming(...args) { ++streamed; return WebAssembly.instantiateStreaming(...args); },
      instantiate(...args) { ++buffered; return WebAssembly.instantiate(...args); }
    },
    async fetch(url, init) {
      const asset = env.omuiAssets.find(a => a.url === url);
      calls.push({ url, id: asset?.id });
      if (asset?.id === 'wasm' && behavior.holdWasm) await new Promise(resolve => { behavior.releaseWasm = resolve; });
      if (behavior.fail === asset?.id) {
        if (behavior.hang) return new Promise((resolve, reject) => init.signal.addEventListener('abort', () => reject(new DOMException('No response', 'AbortError')), { once: true }));
        if (behavior.interrupt) {
          const raw = new Uint8Array(await readFile(join(dirname(fixture), options.prefix || '', url))); let part = 0;
          return new Response(new ReadableStream({ pull(c) { if (!part++) c.enqueue(raw.slice(0, 3)); else c.error(new Error('Connection interrupted')); } }), { headers: { 'Content-Type': 'application/wasm' } });
        }
        return new Response('', { status: behavior.status });
      }
      return fetch(new URL(url, assetBase), init);
    }
  };
  env.window = env;
  const context = vm.createContext(env);
  const selected = options.source || source;
  const code = options.unknown ? selected.replace(/var omuiAssetBytes = [^\n]+;/, 'var omuiAssetBytes = {};') : selected;
  new vm.Script(code, { filename: fixture + ':inline-loader' }).runInContext(context);
  function initialize() {
    if (dependencies.size || initialized) return;
    ++initialized; env.Module.setStatus('Running...'); env.Module.onRuntimeInitialized();
  }
  env.Module.addRunDependency = key => { if (dependencies.has(key)) throw new Error('Duplicate runDependency'); dependencies.add(key); };
  env.Module.removeRunDependency = key => { if (!dependencies.delete(key)) throw new Error('Unknown runDependency'); initialize(); };
  const h = {
    env, events, files, aliases, dependencies, calls, behavior,
    start() { env.Module.instantiateWasm({}, (instance, module) => { check(instance instanceof WebAssembly.Instance && module instanceof WebAssembly.Module, 'the hook supplies a native instance and module'); ++receives; for (const run of env.Module.preRun) run(); initialize(); }); },
    async ready() { await until(() => env.omuiLoad.runtimeInitialized, 'runtime initialization'); },
    async complete() { await until(() => env.omuiLoad.assets.every(a => ['ready', 'skipped'].includes(a.state)), 'all assets'); },
    asset(id) { return env.omuiLoad.assets.find(a => a.id === id); },
    timer(ms) { const hit = [...timers].find(([, t]) => t.ms === ms); if (!hit) throw new Error('Missing timer: ' + ms); timers.delete(hit[0]); hit[1].fn(); },
    present() { env.omuiHasPresented = true; const hook = env.omuiFirstFrame; env.omuiFirstFrame = null; if (typeof hook !== 'function') throw new Error('Native presentation hook missing'); hook(); },
    counts() { return { receives, initialized, streamed, buffered }; },
    dispose() { for (const t of timers.values()) if (t.handle) clearTimeout(t.handle); timers.clear(); }
  };
  instances.push(h); return h;
}
async function scenario(what, run) {
  try { await run(); }
  catch (e) { report.checks.push({ ok: false, what, detail: String(e.stack || e) }); }
}
try {
  await scenario('real Inter initial coverage works for every language and startup failures remain recoverable', async () => {
    const initialSource = loaderSource(await readFile(join(dirname(fixture), 'all-language-initial/index.html'), 'utf8'));
    check(!!initialSource, 'the generator supplies a separate real-TTF all-language fixture');
    for (const language of ['en', 'zh']) {
      const h = harness({ source: initialSource, prefix: 'all-language-initial/', language }); h.env.omuiFontLanguage = language;
      h.behavior.fail = 'initial-font:Inter.ttf'; h.start();
      await until(() => h.asset('initial-font:Inter.ttf')?.state === 'error', 'all-language initial font failure');
      check(h.asset('font:Inter.ttf').group === 'deferred' && !h.calls.some(c => c.id === 'font:Inter.ttf'), language + ': full Inter is deferred without a language rule');
      check(!h.env.omuiLoad.runtimeInitialized && h.dependencies.has('font Inter.ttf') && h.files.has('/fonts/.partial'), 'failed initial Inter retains the real startup dependency and partial marker');
      await sleep(5); h.behavior.fail = null; await h.env.omuiLoad.retry('initial-font:Inter.ttf'); await h.ready();
      check(h.files.get('/fonts/Inter.ttf')?.byteLength === h.asset('initial-font:Inter.ttf').total && h.env.omuiLoad.startup.ratio === 1, 'initial retry installs actual subset bytes before main can start');
      h.present(); await h.complete();
      check(h.files.get('/fonts/Inter.ttf')?.byteLength === h.asset('font:Inter.ttf').total && !h.files.has('/fonts/.partial') && h.files.has('/later/.ready'), 'only the successful post-frame full face replaces initial coverage and clears readiness');
      check(h.aliases.get('/later/fonts/Inter.ttf') === '/fonts/Inter.ttf' && h.counts().initialized === 1, 'full Inter uses the canonical alias without another program initialization');
    }
  });
  await scenario('bootstrap chooses the actual page language after the loader inline script', async () => {
    const h = harness({ browserLanguage: 'zh' });
    check(!h.env.omuiLoad.assets.some(a => a.kind === 'font'), 'inline execution does not prematurely choose fonts from browser preference');
    h.env.omuiFontLanguage = 'en'; h.start(); await h.ready();
    check(h.asset('font:NotoSansSC.ttf').group === 'deferred' && h.asset('initial-font:NotoSansSC.ttf').state === 'ready', 'late English bootstrap overrides Chinese browser preference before the first download plan');
    const firstDownload = h.events.find(e => e.assets.some(a => a.state === 'fetching'));
    check(firstDownload.assets.some(a => a.id === 'initial-font:NotoSansSC.ttf' && a.group === 'startup') && firstDownload.assets.some(a => a.id === 'font:NotoSansSC.ttf' && a.group === 'deferred'), 'the first real fetch event already includes the correct startup font denominator');
  });
  await scenario('static language navigation during wasm fetch replans queued fonts before preRun', async () => {
    for (const [before, after] of [['en', 'zh'], ['zh', 'en']]) {
      const h = harness(); h.env.omuiFontLanguage = before; h.behavior.holdWasm = true; h.start();
      await until(() => typeof h.behavior.releaseWasm === 'function', 'held wasm request');
      check(h.asset('font:NotoSansSC.ttf').group === (before === 'zh' ? 'startup' : 'deferred'), before + ' initial font plan is observable while wasm waits');
      h.env.omuiFontLanguage = after; h.behavior.releaseWasm(); await h.ready();
      const full = h.asset('font:NotoSansSC.ttf'), tiny = h.asset('initial-font:NotoSansSC.ttf');
      check(full.group === (after === 'zh' ? 'startup' : 'deferred') && tiny.state === (after === 'zh' ? 'skipped' : 'ready'), before + '→' + after + ' preRun uses the newly selected language');
      check(new Set(h.env.omuiLoad.assets.map(a => a.id)).size === h.env.omuiLoad.assets.length && h.env.omuiFontPending === (after === 'zh' ? 0 : 1), 'replanning leaves unique assets and the exact deferred-font count');
      check(h.env.omuiLater.filter(f => f[0] === '/later/fonts/NotoSansSC.ttf').length === (after === 'zh' ? 0 : 1), 'replanning never duplicates the later font or retains an obsolete one');
      check(h.files.has('/fonts/.partial') === (after === 'en') && !h.files.has('/later/.ready') && h.env.omuiLoad.startup.ratio === 1, 'partial/deferred readiness and startup bytes reflect the final plan');
      check(!h.calls.some(c => c.url === (after === 'zh' ? tiny.url : full.url)), 'obsolete queued font assets are not fetched');
      h.present(); await h.complete(); check(h.env.omuiFontPending === 0 && !h.files.has('/fonts/.partial') && h.counts().initialized === 1, 'the replanned later chain completes without duplicate initialization');
    }
  });
  await scenario('streamed gzip package, deferred fonts, and synchronous native presentation', async () => {
    const h = harness(); h.start(); await h.ready();
    check(h.counts().streamed === 1 && h.counts().buffered === 0, 'normal wasm retains native streaming instantiation');
    check(h.env.omuiLoad.basis === 'decoded' && h.env.omuiLoad.startup.ratio === 1 && h.env.omuiLoad.ratio < 1, 'startup and all-managed totals are distinct decoded-byte scopes');
    check(h.files.has('/fonts/.partial') && !h.files.has('/later/.ready'), 'deferred full fonts do not claim readiness');
    const before = h.events.length; h.present();
    check(h.events.slice(before).some(e => e.phase === 'presented' && e.presented), 'the native first-frame callback synchronously emits presented');
    await h.complete();
    check(h.env.omuiLoad.ratio === 1 && h.files.has('/later/.ready') && !h.files.has('/fonts/.partial'), 'successful deferred installation alone clears partial and writes ready');
    check(h.aliases.get('/later/fonts/NotoSansSC.ttf') === '/fonts/NotoSansSC.ttf', 'deferred font uses the canonical full font without duplicate MEMFS data');
    const count = h.calls.length; h.env.omuiFetchLater(); await h.env.omuiLoad.retry();
    check(h.calls.length === count && h.counts().receives === 1 && h.counts().initialized === 1, 'completed work and main initialization never restart');
    check(requests.some(r => r.encoded !== r.raw) && h.asset('wasm').loaded === h.asset('wasm').total, 'real HTTP gzip Content-Length is not confused with decoded progress');
  });
  await scenario('four-second fallback is not a presented frame', async () => {
    const h = harness(); h.start(); await h.ready(); h.timer(4000); await h.complete();
    check(!h.env.omuiLoad.presented && !h.events.some(e => e.phase === 'presented') && typeof h.env.omuiFirstFrame === 'function', 'fallback fetches preserve the real native presentation hook');
    const count = h.calls.length; h.present(); h.env.omuiFetchLater();
    check(h.events.filter(e => e.phase === 'presented').length === 1 && h.calls.length === count, 'a later real presentation is reported once without fetching twice');
  });
  await scenario('Chinese opening selects full rather than initial coverage', async () => {
    const h = harness({ language: 'zh' }); h.start(); await h.ready();
    check(h.asset('font:NotoSansSC.ttf').group === 'startup' && h.asset('initial-font:NotoSansSC.ttf').state === 'skipped', 'a Chinese visit installs full CJK before initialization');
    check(!h.calls.some(c => c.url === h.asset('initial-font:NotoSansSC.ttf').url), 'unused initial coverage does not fetch or inflate the denominator');
  });
  await scenario('no ReadableStream has an honest buffered fallback', async () => {
    const h = harness({ noStream: true }); h.start(); await h.ready();
    check(h.counts().streamed === 0 && h.counts().buffered === 1 && h.env.omuiLoad.startup.ratio === 1, 'buffered fallback initializes once and counts only actual completed bytes');
  });
  await scenario('unknown totals remain indeterminate until actual EOF', async () => {
    const h = harness({ unknown: true });
    check(h.env.omuiLoad.total === null && h.env.omuiLoad.ratio === null && h.env.omuiLoad.startup.ratio === null, 'no metadata means null denominators, not a guessed percentage');
    const initial = h.env.omuiLoad.loaded; h.env.Module.setStatus('Downloading (99/100)');
    check(h.env.omuiLoad.loaded === initial && h.env.omuiLoad.ratio === null, 'Module.setStatus text is not parsed into progress');
    h.start(); await h.ready(); check(h.env.omuiLoad.startup.ratio === 1 && h.env.omuiLoad.ratio === null, 'EOF establishes actual startup sizes while deferred totals stay unknown');
    h.present(); await h.complete(); check(h.env.omuiLoad.ratio === 1, 'all actual EOF sizes eventually give a factual complete ratio');
  });
  await scenario('startup font failure retains dependency and can retry without reinitialization', async () => {
    const h = harness(); h.behavior.fail = 'font:Inter.ttf'; h.start();
    await until(() => h.asset('font:Inter.ttf').state === 'error', 'startup font error');
    check(!h.env.omuiLoad.runtimeInitialized && h.dependencies.has('font Inter.ttf') && !h.files.has('/fonts/Inter.ttf'), 'failed startup font cannot release a false-ready dependency');
    check(h.asset('font:Inter.ttf').attempt === 1 && h.env.omuiLoad.error.retryable, 'HTTP 404 is explicit and does not auto-retry forever');
    await sleep(5); h.behavior.fail = null; await h.env.omuiLoad.retry('font:Inter.ttf'); await h.ready();
    check(h.counts().receives === 1 && h.counts().initialized === 1 && h.dependencies.size === 0, 'explicit font retry completes the original waiting initialization');
  });
  await scenario('failed deferred font preserves partial and omits ready until retry', async () => {
    const h = harness(); h.start(); await h.ready(); h.behavior.fail = 'font:NotoSansSC.ttf'; h.present();
    await until(() => h.asset('font:NotoSansSC.ttf').state === 'error', 'deferred font error');
    check(h.files.has('/fonts/.partial') && !h.files.has('/later/.ready') && h.env.omuiLoad.presented, 'background failure neither lies about readiness nor restarts the running program');
    await sleep(5); h.behavior.fail = null; await h.env.omuiLoad.retry('font:NotoSansSC.ttf'); await h.complete();
    check(!h.files.has('/fonts/.partial') && h.files.has('/later/.ready') && h.counts().initialized === 1, 'deferred retry installs once and then reports readiness');
  });
  await scenario('interrupted wasm streams retry a bounded number and remain explicitly recoverable', async () => {
    const h = harness(); h.behavior.fail = 'wasm'; h.behavior.interrupt = true; h.start();
    await until(() => h.asset('wasm').attempt === 3 && !h.env.omuiJobs.wasm.running, 'bounded interrupted fetches');
    check(h.asset('wasm').state === 'error' && h.counts().receives === 0 && !h.env.omuiLoad.runtimeInitialized, 'three interrupted attempts fail visibly without starting main');
    h.behavior.fail = null; await h.env.omuiLoad.retry('wasm'); await h.ready();
    check(h.counts().receives === 1 && h.counts().initialized === 1, 'explicit wasm retry recovers the original hook exactly once');
  });
  await scenario('a silent connection has a bounded inactivity failure', async () => {
    const h = harness(); h.behavior.fail = 'wasm'; h.behavior.hang = true; h.start();
    for (let n = 1; n <= 3; ++n) { await until(() => h.asset('wasm').attempt === n, 'timeout attempt'); h.timer(15000); }
    await until(() => !h.env.omuiJobs.wasm.running, 'final inactivity error');
    check(h.asset('wasm').state === 'error' && h.asset('wasm').attempt === 3, 'inactivity cannot leave an unreported permanent spinner');
  });
  await scenario('fatal runtime or launcher failures require reload rather than another main', async () => {
    const h = harness(); h.start(); await h.ready(); const calls = h.calls.length;
    h.env.Module.onAbort('Runtime failed'); const retry = await h.env.omuiLoad.retry();
    check(h.env.omuiLoad.phase === 'error' && retry.reason === 'reload_required' && h.calls.length === calls && h.counts().initialized === 1, 'runtime abort cannot duplicate program initialization');
    const launcher = harness(); launcher.env.dispatchEvent({ type: 'error', target: { src: new URL(launcher.env.omuiLauncher, base).href } });
    check(launcher.env.omuiLoad.phase === 'error' && launcher.env.omuiLoad.error.id === 'launcher', 'launcher network failure leaves a readable explicit error state');
  });
} finally {
  for (const h of instances) h.dispose();
  server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
}
report.ok = report.checks.length > 0 && report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2)); process.exit(report.ok ? 0 : 1);
