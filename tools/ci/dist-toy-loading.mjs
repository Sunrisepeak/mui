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
    res.writeHead(200, { 'Content-Type': extname(name) === '.wasm' ? 'application/wasm' : 'application/octet-stream', 'Content-Encoding': 'gzip', 'Content-Length': encoded.length, 'Cache-Control': 'no-store' });
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
  const behavior = { fail: null, status: 404, interrupt: false, hang: false, holdWasm: false, releaseWasm: null, holdFont: null, releaseFont: null, failWrite: null, writeFailures: 0 };
  const env = {
    console, URL, Headers, Response, ReadableStream: options.noStream ? undefined : ReadableStream,
    AbortController, Uint8Array, setTimeout(fn, ms) {
      const id = ++serial; const entry = { fn, ms }; timers.set(id, entry);
      // Backoff is accelerated; inactivity and first-frame timers are fired
      // explicitly by tests, rather than waiting 15/60 seconds or 4 seconds.
      if (ms < 4000 && !(options.holdBackoff && (ms === 250 || ms === 1000))) entry.handle = setTimeout(() => { timers.delete(id); fn(); }, 0);
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
      readFile(path) { if (!files.has(path)) throw new Error('No such file: ' + path); return files.get(path); },
      writeFile(path, bytes) { if (behavior.failWrite === path) { ++behavior.writeFailures; throw new Error('Injected FS installation failure: ' + path); } files.set(path, typeof bytes === 'string' ? new TextEncoder().encode(bytes) : new Uint8Array(bytes)); },
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
      if (asset?.id === behavior.holdFont) await new Promise(resolve => { behavior.releaseFont = resolve; });
      if (asset?.id === 'wasm' && behavior.holdWasm) await new Promise(resolve => { behavior.releaseWasm = resolve; });
      if (behavior.fail === asset?.id) {
        if (behavior.hang) return new Promise((resolve, reject) => init.signal.addEventListener('abort', () => reject(new DOMException('No response', 'AbortError')), { once: true }));
        if (behavior.interrupt) {
          const raw = new Uint8Array(await readFile(join(dirname(fixture), options.prefix || '', url))); let part = 0;
          return new Response(new ReadableStream({ pull(c) { if (!part++) c.enqueue(raw.slice(0, 3)); else c.error(new Error('Connection interrupted')); } }), { headers: { 'Content-Type': 'application/wasm' } });
        }
        return new Response('', { status: behavior.status });
      }
      return fetch(new URL(url, assetBase), { ...init, cache: 'no-store' });
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
    async complete() { await until(() => env.omuiLoad.assets.every(a => ['ready', 'skipped', 'retired'].includes(a.state)), 'all assets'); },
    asset(id) { return env.omuiLoad.assets.find(a => a.id === id); },
    hasTimer(ms) { return [...timers.values()].some(t => t.ms === ms); },
    timer(ms) { const hit = [...timers].find(([, t]) => t.ms === ms); if (!hit) throw new Error('Missing timer: ' + ms); timers.delete(hit[0]); hit[1].fn(); },
    // Drawn; then, unless told otherwise, the program asks for every face (another
    // language's faces wait until it does: a reader drawing in all of them).
    present() { env.omuiHasPresented = true; const hook = env.omuiFirstFrame; env.omuiFirstFrame = null; if (typeof hook !== 'function') throw new Error('Native presentation hook missing'); hook(); if (options.asks !== false) env.omuiWantFonts(); },
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
  await scenario('content-identical font basenames share transfer bytes but retain independent installations', async () => {
    const aliasSource = loaderSource(await readFile(join(dirname(fixture), 'shared-font-url/index.html'), 'utf8'));
    check(!!aliasSource, 'the actual generator provides two selected basenames for one physical font URL');
    const h = harness({ source: aliasSource, prefix: 'shared-font-url/' });
    h.behavior.holdFont = 'font:Inter.ttf';
    const aPromise = h.env.omuiLoad.fontBytes('Inter.ttf'), bPromise = h.env.omuiLoad.fontBytes('InterAlias.ttf');
    await until(() => typeof h.behavior.releaseFont === 'function', 'held shared transfer'); h.start();
    await until(() => h.dependencies.size === 2, 'both font installation dependencies');
    check(h.dependencies.has('font Inter.ttf') && h.dependencies.has('font InterAlias.ttf') && !h.env.omuiLoad.runtimeInitialized, 'both basenames hold their own real startup dependency');
    check(h.calls.filter(c => c.id === 'font:Inter.ttf').length === 1 && h.env.omuiLoad.assets.filter(a => a.kind === 'font').length === 1, 'one URL has one fetch and one decoded-byte progress entry');
    h.behavior.releaseFont(); const [a, b] = await Promise.all([aPromise, bPromise]); await h.ready();
    check(a.url === b.url && a.id === 'font:Inter.ttf' && b.id === 'font:InterAlias.ttf', 'leases preserve the requested logical name while reporting the actual shared content URL');
    check(h.files.get('/fonts/Inter.ttf')?.byteLength === a.buffer.byteLength && h.files.get('/fonts/InterAlias.ttf')?.byteLength === b.buffer.byteLength && !h.dependencies.size, 'each selected font is installed at its own canonical path before main');
    a.release(); b.release(); h.present(); await h.complete();
    check(h.env.omuiLoad.phase === 'complete' && h.env.omuiLoad.ratio === 1 && h.counts().initialized === 1, 'shared transfer installation completes once with an unduplicated byte denominator');
    const late = await h.env.omuiLoad.fontBytes('InterAlias.ttf'); late.release();
    check(h.calls.filter(c => c.url === a.url).length === 1 && Object.values(h.env.omuiFontRecords).every(r => r.buffer === null), 'late alias reads use its own installed FS file without a second transfer or retained byte cache');
    const mixedFonts = h.env.omuiFonts.map(f => f[0] === 'InterAlias.ttf' ? [f[0], f[1], ['zh']] : f);
    const mixed = harness({ source: aliasSource.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(mixedFonts) + ';'), prefix: 'shared-font-url/' });
    mixed.start(); await mixed.ready();
    check(mixed.files.has('/fonts/Inter.ttf') && !mixed.files.has('/fonts/InterAlias.ttf') && mixed.env.omuiFontPending === 1, 'same-URL fonts keep independent startup and deferred installation roles');
    mixed.present(); await mixed.complete();
    check(mixed.files.has('/fonts/InterAlias.ttf') && mixed.aliases.get('/later/fonts/InterAlias.ttf') === '/fonts/InterAlias.ttf' && mixed.calls.filter(c => c.url === a.url).length === 1, 'the deferred alias installs after presentation using the already transferred content');
  });
  await scenario('retrying a shared physical asset restores every currently eligible font installation', async () => {
    const aliasSource = loaderSource(await readFile(join(dirname(fixture), 'shared-font-url/index.html'), 'utf8'));
    const probe = harness({ source: aliasSource, prefix: 'shared-font-url/' });
    const mixedFonts = probe.env.omuiFonts.map(f => f[0] === 'Inter.ttf' ? [f[0], f[1], ['zh']] : f);
    const h = harness({ source: aliasSource.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(mixedFonts) + ';'), prefix: 'shared-font-url/' });
    h.behavior.fail = 'font:Inter.ttf'; h.start();
    await until(() => h.env.omuiLoad.error && !h.env.omuiFontRecords[h.env.omuiLoad.error.url].transfer.running, 'shared physical network failure');
    const failure = h.env.omuiLoad.error;
    check(failure.id === 'font:Inter.ttf' && h.env.omuiFontTargets[failure.id].role === 'later' && h.dependencies.has('font InterAlias.ttf') && !h.env.omuiLoad.runtimeInitialized, 'a physical later asset reports the shared failure while its startup alias still holds the dependency');
    h.behavior.fail = null; await h.env.omuiLoad.retry(failure.id); await h.ready();
    check(h.files.has('/fonts/InterAlias.ttf') && !h.files.has('/fonts/Inter.ttf') && !h.dependencies.size && h.env.omuiFontPending === 1, 'retry(error.id) installs the eligible startup alias without prematurely installing its later canonical target');
    h.present(); await h.complete();
    check(h.files.has('/fonts/Inter.ttf') && h.aliases.get('/later/fonts/Inter.ttf') === '/fonts/Inter.ttf' && h.calls.filter(c => c.url === failure.url).length === 2, 'post-frame canonical installation reuses the one successful retry transfer');
    check(h.env.omuiLoad.phase === 'complete' && h.counts().initialized === 1 && !h.files.has('/fonts/.partial'), 'shared-role network recovery completes with one program initialization');
  });
  await scenario('a single failed alias FS installation recovers by the reported physical error ID without refetching', async () => {
    const aliasSource = loaderSource(await readFile(join(dirname(fixture), 'shared-font-url/index.html'), 'utf8'));
    const h = harness({ source: aliasSource, prefix: 'shared-font-url/' });
    h.behavior.failWrite = '/fonts/InterAlias.ttf'; h.start();
    await until(() => h.behavior.writeFailures && h.env.omuiLoad.error && !h.env.omuiFontRecords[h.env.omuiLoad.error.url].transfer.running, 'alias FS installation failure');
    const failure = h.env.omuiLoad.error, count = h.calls.length;
    check(failure.id === 'font:Inter.ttf' && h.files.has('/fonts/Inter.ttf') && !h.files.has('/fonts/InterAlias.ttf') && h.dependencies.has('font InterAlias.ttf'), 'the physical asset error preserves the failed alias dependency after its canonical sibling installed');
    h.behavior.failWrite = null; await h.env.omuiLoad.retry(failure.id); await h.ready();
    check(h.files.has('/fonts/InterAlias.ttf') && !h.dependencies.size && h.calls.length === count && h.asset(failure.id).attempt === 1, 'retry(error.id) resumes the failed FS alias using existing decoded bytes without another network attempt');
    h.present(); await h.complete();
    check(h.env.omuiLoad.phase === 'complete' && h.counts().initialized === 1 && Object.values(h.env.omuiFontRecords).every(r => r.buffer === null), 'FS recovery completes once and releases the private transfer bytes');
  });
  await scenario('a failed early face retired by locale navigation cannot block the current full-face plan', async () => {
    const h = harness(); h.env.omuiFontLanguage = 'en'; h.behavior.fail = 'initial-font:NotoSansSC.ttf';
    let failure; try { await h.env.omuiLoad.fontBytes('NotoSansSC.ttf'); } catch (error) { failure = error; }
    check(failure?.id === 'initial-font:NotoSansSC.ttf' && h.env.omuiLoad.error, 'the required initial-face failure is visible before the language changes');
    h.behavior.holdWasm = true; h.start(); await until(() => typeof h.behavior.releaseWasm === 'function', 'locale switch after failed early font');
    h.env.omuiFontLanguage = 'zh'; h.behavior.releaseWasm(); await h.ready();
    const old = h.asset('initial-font:NotoSansSC.ttf');
    check(old.state === 'retired' && old.transferState === 'error' && old.error?.id === old.id && old.attempt === 1, 'retired optional font keeps its actual failed-transfer history without another request');
    check(h.env.omuiLoad.error === null && h.asset('font:NotoSansSC.ttf').state === 'ready' && h.env.omuiLoad.startup.ratio === 1, 'the currently required full face succeeds without inheriting an obsolete failure');
    h.present(); await h.complete(); const count = h.calls.length; const retry = await h.env.omuiLoad.retry();
    check(h.env.omuiLoad.phase === 'complete' && h.env.omuiLoad.ratio === 1 && h.env.omuiFontPending === 0 && !h.files.has('/fonts/.partial'), 'retired history does not block current-plan progress or complete');
    check(h.calls.length === count && retry.blocked.includes(old.id) && h.counts().initialized === 1, 'retry reports the retired failure as blocked instead of refetching an obsolete face');
    const required = harness(); required.env.omuiFontLanguage = 'en'; required.behavior.fail = 'initial-font:NotoSansSC.ttf';
    try { await required.env.omuiLoad.fontBytes('NotoSansSC.ttf'); } catch {}
    required.env.omuiFontLanguage = 'zh'; required.behavior.fail = 'font:NotoSansSC.ttf'; required.start();
    await until(() => required.asset('font:NotoSansSC.ttf')?.state === 'error' && !required.env.omuiFontRecords[required.asset('font:NotoSansSC.ttf').url].transfer.running, 'current required face failure');
    check(required.env.omuiLoad.error?.id === 'font:NotoSansSC.ttf' && required.dependencies.has('font NotoSansSC.ttf') && !required.env.omuiLoad.runtimeInitialized, 'retiring the obsolete face never hides a failure in the newly required face');
    required.behavior.fail = null; await required.env.omuiLoad.retry('font:NotoSansSC.ttf'); await required.ready(); required.present(); await required.complete();
    check(required.env.omuiLoad.phase === 'complete' && required.counts().initialized === 1, 'the current required failure still needs a successful explicit retry before completion');
  });
  await scenario('locale retirement during retry backoff never starts another obsolete transfer', async () => {
    const h = harness({ holdBackoff: true }); h.env.omuiFontLanguage = 'en';
    h.behavior.fail = 'initial-font:NotoSansSC.ttf'; h.behavior.status = 503;
    const result = h.env.omuiLoad.fontBytes('NotoSansSC.ttf').catch(error => error);
    await until(() => h.asset('initial-font:NotoSansSC.ttf')?.state === 'error' && h.hasTimer(250), 'explicitly held automatic retry backoff');
    const url = h.asset('initial-font:NotoSansSC.ttf').url, transfer = h.env.omuiFontRecords[url].transfer;
    check(h.calls.filter(c => c.url === url).length === 1 && h.asset('initial-font:NotoSansSC.ttf').attempt === 1, 'the retryable early initial failure is held between its first request and automatic retry');
    h.env.omuiFontLanguage = 'zh'; h.start(); await h.ready(); h.present(); await h.complete();
    check(h.asset('initial-font:NotoSansSC.ttf').state === 'retired' && h.env.omuiLoad.phase === 'complete', 'the full-face locale plan can complete while the obsolete retry timer remains pending');
    const count = h.calls.length; h.timer(250);
    await until(() => h.calls.length > count || !transfer.running, 'old retry timer resumes after retirement');
    check(h.calls.length === count && h.asset('initial-font:NotoSansSC.ttf').attempt === 1, 'a retired asset is checked again after backoff before creating a controller or issuing another request');
    const failure = await result;
    check(failure.code === 'font_fetch_failed' && failure.id === 'initial-font:NotoSansSC.ttf', 'the original DOM consumer receives its factual failure without reviving a retired request');
    check(h.env.omuiLoad.phase === 'complete' && h.env.omuiLoad.error === null && h.counts().initialized === 1 && !h.hasTimer(1000), 'retired retry completion does not disturb the current plan or schedule another backoff');
  });
  await scenario('explicit DOM byte leases share a single decoded transfer with native startup and late full faces', async () => {
    const initialSource = loaderSource(await readFile(join(dirname(fixture), 'all-language-initial/index.html'), 'utf8'));
    const h = harness({ source: initialSource, prefix: 'all-language-initial/' });
    const beforeRequests = requests.length;
    const firstPromise = h.env.omuiLoad.fontBytes('Inter.ttf'), secondPromise = h.env.omuiLoad.fontBytes('Inter.ttf');
    check(h.calls.length === 1 && h.calls[0].id === 'initial-font:Inter.ttf', 'explicit DOM consumers begin the initial face before wasm starts');
    const [first, second] = await Promise.all([firstPromise, secondPromise]);
    const url = first.url, record = h.env.omuiFontRecords[url];
    check(first.buffer !== second.buffer && first.buffer.byteLength === h.asset(first.id).total && !h.files.has('/fonts/Inter.ttf'), 'concurrent leases own isolated copies of one decoded transfer without falsely installing FS or becoming ready');
    check(h.asset(first.id).state === 'downloaded' && h.env.omuiLoad.startup.ratio < 1, 'prefetch reports factual downloaded bytes while wasm and FS are not ready');
    first.release(); first.release(); check(first.buffer === null && record.leases === 1 && record.buffer !== second.buffer && record.buffer !== null, 'release is idempotent and the internal transfer bytes remain private');
    new Uint8Array(second.buffer)[0] ^= 255;
    const originalByte = new Uint8Array(record.buffer)[0];
    second.release();
    check(record.buffer !== null && !record.leases && !h.files.has('/fonts/Inter.ttf'), 'releasing every early DOM lease still retains bytes until native FS installation');
    const third = await h.env.omuiLoad.fontBytes('Inter.ttf');
    h.start(); await h.ready();
    check(h.files.get('/fonts/Inter.ttf')[0] === originalByte && new Uint8Array(third.buffer)[0] === originalByte, 'mutating a public lease cannot corrupt another lease or native startup font bytes');
    check(h.calls.filter(c => c.url === url).length === 1 && requests.slice(beforeRequests).filter(r => 'all-language-initial/' + url === r.name).length === 1, 'DOM plus FS use one actual HTTP request with cache disabled');
    check(record.buffer !== third.buffer && h.files.get('/fonts/Inter.ttf').byteLength === third.buffer.byteLength, 'native installation preserves the isolated active DOM lease');
    third.buffer = null; third.release(); third.release(); check(record.buffer === null && record.transfer.running === null, 'installed bytes and settled transfer references are released after the final DOM lease');
    const initialAgain = await h.env.omuiLoad.fontBytes('Inter.ttf');
    check(initialAgain.id === 'initial-font:Inter.ttf' && initialAgain.url === url && h.calls.filter(c => c.url === url).length === 1, 'a late initial lease reads the canonical installed bytes without refetch');
    h.present(); await h.complete();
    const full = await h.env.omuiLoad.fontBytes('Inter.ttf');
    check(full.id === 'font:Inter.ttf' && full.url === h.asset('font:Inter.ttf').url && full.buffer.byteLength === h.asset('font:Inter.ttf').total, 'after canonical replacement late consumers receive the actual full face and URL');
    check(initialAgain.buffer.byteLength === h.asset('initial-font:Inter.ttf').total && initialAgain.buffer.byteLength < full.buffer.byteLength, 'an outstanding initial lease remains the initial face when native installs the full font');
    initialAgain.release(); full.release();
    check(Object.values(h.env.omuiFontRecords).every(r => r.buffer === null) && h.counts().initialized === 1, 'all installed font buffers are released without a second main initialization');
  });
  await scenario('locale replans preserve early in-flight transfers without duplicating or installing the wrong face', async () => {
    for (const [before, after] of [['en', 'zh'], ['zh', 'en']]) {
      const h = harness(); h.env.omuiFontLanguage = before;
      h.behavior.holdFont = (before === 'en' ? 'initial-font:' : 'font:') + 'NotoSansSC.ttf';
      const leasePromise = h.env.omuiLoad.fontBytes('NotoSansSC.ttf');
      await until(() => typeof h.behavior.releaseFont === 'function', 'held explicit font request');
      const original = h.asset(h.behavior.holdFont), record = h.env.omuiFontRecords[original.url];
      h.behavior.holdWasm = true; h.start(); await until(() => typeof h.behavior.releaseWasm === 'function', 'held replan wasm');
      h.env.omuiFontLanguage = after; h.behavior.releaseWasm(); await h.ready();
      check(h.env.omuiFontRecords[original.url] === record && h.calls.filter(c => c.url === original.url).length === 1, before + '→' + after + ': preRun preserves the actual in-flight URL record');
      h.behavior.releaseFont(); const lease = await leasePromise;
      check(lease.url === original.url && lease.buffer.byteLength === h.asset(original.id).total, 'the original DOM request resolves to its original bytes, never a substituted locale version');
      check(h.files.get('/fonts/NotoSansSC.ttf').byteLength === h.asset(after === 'en' ? 'initial-font:NotoSansSC.ttf' : 'font:NotoSansSC.ttf').total, 'only the final startup plan installs before presentation');
      lease.release(); h.present(); await h.complete();
      check(h.calls.filter(c => c.url === original.url).length === 1 && h.env.omuiFontPending === 0 && h.counts().initialized === 1, 'the preserved transfer completes its final role with one download and exact readiness');
    }
  });
  await scenario('early DOM transfer errors stay explicit and retry joins the original runtime dependency', async () => {
    const h = harness(); h.behavior.fail = 'font:Inter.ttf';
    let failure; try { await h.env.omuiLoad.fontBytes('Inter.ttf'); } catch (error) { failure = error; }
    check(failure?.code === 'font_fetch_failed' && failure.id === 'font:Inter.ttf' && failure.retryable && h.env.omuiLoad.phase === 'error', 'early network failure rejects the DOM consumer with a retryable asset error');
    h.start(); await until(() => h.dependencies.has('font Inter.ttf') && h.asset('font:Inter.ttf').state === 'error' && !h.env.omuiFontRecords[h.asset('font:Inter.ttf').url].transfer.running, 'failed prefetched startup dependency');
    check(!h.env.omuiLoad.runtimeInitialized && !h.files.has('/fonts/Inter.ttf'), 'failed prefetch cannot release native startup as falsely ready');
    h.behavior.fail = null; await h.env.omuiLoad.retry('font:Inter.ttf'); await h.ready();
    const lease = await h.env.omuiLoad.fontBytes('Inter.ttf'); lease.release();
    check(h.counts().initialized === 1 && !h.dependencies.size && h.calls.filter(c => c.id === 'font:Inter.ttf').length === 2, 'explicit retry installs once and a new DOM call reuses FS without another successful transfer');
    let missing; try { await h.env.omuiLoad.fontBytes('Unknown.ttf'); } catch (error) { missing = error; }
    check(missing?.code === 'font_not_found', 'unpackaged font names reject without a network request');
    h.env.Module.onAbort('Stopped'); let fatal;
    try { await h.env.omuiLoad.fontBytes('Inter.ttf'); } catch (error) { fatal = error; }
    check(fatal?.code === 'reload_required', 'fatal runtime errors block new DOM transfer requests');
    const noInitial = h.env.omuiFonts.map(f => f[0] === 'NotoSansSC.ttf' ? f.slice(0, 3) : f);
    const deferred = harness({ source: source.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(noInitial) + ';') });
    let wait; try { await deferred.env.omuiLoad.fontBytes('NotoSansSC.ttf'); } catch (error) { wait = error; }
    check(wait?.code === 'font_deferred' && deferred.calls.length === 0, 'a language-deferred face without initial coverage does not silently fetch before presentation');
  });
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
  await scenario('another language\'s face waits until the program asks, and loading still completes', async () => {
    const h = harness({ asks: false }); h.start(); await h.ready(); h.present();
    await until(() => h.env.omuiLoad.phase === 'complete', 'complete without the face');
    check(!h.calls.some(c => c.id === 'font:NotoSansSC.ttf') && h.asset('font:NotoSansSC.ttf')?.state === 'deferred',
          'unasked, another language\'s face is not fetched');
    const announced = () => new TextDecoder().decode(h.files.get('/later/.ready') || new Uint8Array());
    const before = announced();
    h.env.omuiWantFonts(0x1f600);
    check(!h.calls.some(c => c.id === 'font:NotoSansSC.ttf'), 'a character it cannot draw asks nothing of it');
    h.env.omuiWantFonts(0x4e2d); await h.complete();
    check(h.calls.filter(c => c.id === 'font:NotoSansSC.ttf').length === 1 && announced() !== before && h.aliases.get('/later/fonts/NotoSansSC.ttf') === '/fonts/NotoSansSC.ttf',
          'a character it can draw fetches it once, announced in a new batch');
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
    const h = harness(); h.start(); await h.ready(); h.timer(4000); h.env.omuiWantFonts(); await h.complete();
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
