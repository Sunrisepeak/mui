// The visitor's path and the generated loader in a real browser (moved from
// tools/ci/visitor-path.sh, visitor-path.mjs and dist-toy-loading.mjs, D-206).
//
// visitor-path  the built site in a real browser as a person opens it, and that person's own agent
// operating it the
//               way the page's prompt tells an agent to (D-95; W3's CI case): "Copy the prompt"
//               pressed with real clicks gives a pairing code and the page's agent.md; the agent
//               reads it, starts the bridge with it
//               (`omui connect --code … --name …`), waits for the page (`omui page status --wait`),
//               does agent.md's self-check (which the person's log then shows), reads and operates
//               the page with `omui page …` (its semantic tree, an action, the interaction sweep
//               over every control); both ends say how it stands, and when the bridge stops the
//               page knows. The report: target/visitor-path.json.
// dist-toy-loading  the actual generated inline loader (omui.gen's dist_toy test writes the
// fixture) executed against
//               the browser's own fetch, Response, ReadableStream and WebAssembly over real gzip
//               HTTP; the launcher lifecycle and MEMFS are small test doubles, each loader in a
//               frame of its own (node's vm context before); this does not claim to execute a
//               complete Emscripten app. The report: target/dist-toy-loading.report.xdef.json.
module omui.devtools.check;

import std;
import mcpplibs.os;
import mlibs.http;
import omui.devtools.browser;
import omui.devtools.cdp;
import omui.devtools.run;
import :pages;
import :support;

namespace omui::devtools::check {

namespace fs = std::filesystem;
using namespace support;

namespace {

using cdp::json, cdp::at, cdp::dig, cdp::q, cdp::text, cdp::truthy, cdp::number;

void write_json(const fs::path& path, const json& report, int indent = 2) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream(path, std::ios::binary) << cdp::pretty(report, indent) << '\n';
}

// The loader's harness, evaluated in the page (the allowlist's browser host JavaScript):
// `@FIXTURE@` is the fixture's name in the report.
constexpr std::string_view loader_harness = R"js((async () => {
try {
const fixture = @FIXTURE@;
const base = location.origin + '/';
// The fixture's own files, read as they are (not counted among the loader's requests).
const fixtureText = async name => (await fetch(new URL(name, base).href + '?omui-unlogged', { cache: 'no-store' })).text();
const serverRequests = async () => (await fetch('/__omui/requests', { cache: 'no-store' })).json();
const html = await fixtureText('index.html');
const loaderSource = html => [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].map(m => m[1]).find(s => s.includes('var omuiWasm =') && s.includes('window.omuiLoad ='));
const source = loaderSource(html);
if (!source) return { error: 'Fixture lacks the managed loader; run the current gen.dist_toy tests first' };
const report = { fixture, checks: [] };
function check(ok, what, detail) { report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : { detail }) }); if (!ok) throw new Error(what); }
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, what) {
  const end = Date.now() + 4000;
  while (Date.now() < end) { if (await fn()) return; await sleep(5); }
  throw new Error('Timed out: ' + what);
}
const instances = [];

// The loader's own global: a frame of its own at the asset base (its document.URL carries the route), where the
// browser's interfaces it uses are the test doubles and this page's own natives, as node's vm context had them.
async function harness(options = {}) {
  const assetBase = new URL(options.prefix || '', base);
  const events = [], listeners = new Map(), files = new Map(), aliases = new Map(), dependencies = new Set(), calls = [];
  const timers = new Map(); let serial = 0, receives = 0, initialized = 0, streamed = 0, buffered = 0;
  const status = { textContent: '', style: {} }, canvas = { focus() {}, addEventListener() {} };
  const behavior = { fail: null, status: 404, interrupt: false, hang: false, holdWasm: false, releaseWasm: null, holdFont: null, releaseFont: null, failWrite: null, writeFailures: 0 };
  const frame = document.createElement('iframe');
  frame.style.display = 'none';
  const loaded = new Promise(resolve => frame.addEventListener('load', resolve, { once: true }));
  frame.src = assetBase.href + '?omui-blank#/' + (options.language || 'en');
  document.body.appendChild(frame);
  await loaded;
  const env = frame.contentWindow;
  const doubles = {
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
          const raw = new Uint8Array(await (await fetch(new URL(url, assetBase).href + '?omui-unlogged', { cache: 'no-store' })).arrayBuffer()); let part = 0;
          return new Response(new ReadableStream({ pull(c) { if (!part++) c.enqueue(raw.slice(0, 3)); else c.error(new Error('Connection interrupted')); } }), { headers: { 'Content-Type': 'application/wasm' } });
        }
        return new Response('', { status: behavior.status });
      }
      return fetch(new URL(url, assetBase), { ...init, cache: 'no-store' });
    }
  };
  for (const [name, value] of Object.entries(doubles)) Object.defineProperty(env, name, { value, writable: true, configurable: true });
  env.document.getElementById = id => id === 'canvas' ? canvas : status;
  Object.defineProperty(env.navigator, 'languages', { get: () => [options.browserLanguage || 'en'], configurable: true });
  Object.defineProperty(env.navigator, 'language', { get: () => options.browserLanguage || 'en', configurable: true });
  const selected = options.source || source;
  const code = options.unknown ? selected.replace(/var omuiAssetBytes = [^\n]+;/, 'var omuiAssetBytes = {};') : selected;
  env.eval(code);
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
    dispose() { for (const t of timers.values()) if (t.handle) clearTimeout(t.handle); timers.clear(); frame.remove(); }
  };
  instances.push(h); return h;
}
async function scenario(what, run) {
  try { await run(); }
  catch (e) { report.checks.push({ ok: false, what, detail: String(e.stack || e) }); }
}
await scenario('content-identical font basenames share transfer bytes but retain independent installations', async () => {
    const aliasSource = loaderSource(await fixtureText('shared-font-url/index.html'));
    check(!!aliasSource, 'the actual generator provides two selected basenames for one physical font URL');
    const h = await harness({ source: aliasSource, prefix: 'shared-font-url/' });
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
    const mixed = await harness({ source: aliasSource.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(mixedFonts) + ';'), prefix: 'shared-font-url/' });
    mixed.start(); await mixed.ready();
    check(mixed.files.has('/fonts/Inter.ttf') && !mixed.files.has('/fonts/InterAlias.ttf') && mixed.env.omuiFontPending === 1, 'same-URL fonts keep independent startup and deferred installation roles');
    mixed.present(); await mixed.complete();
    check(mixed.files.has('/fonts/InterAlias.ttf') && mixed.aliases.get('/later/fonts/InterAlias.ttf') === '/fonts/InterAlias.ttf' && mixed.calls.filter(c => c.url === a.url).length === 1, 'the deferred alias installs after presentation using the already transferred content');
  });
  await scenario('retrying a shared physical asset restores every currently eligible font installation', async () => {
    const aliasSource = loaderSource(await fixtureText('shared-font-url/index.html'));
    const probe = await harness({ source: aliasSource, prefix: 'shared-font-url/' });
    const mixedFonts = probe.env.omuiFonts.map(f => f[0] === 'Inter.ttf' ? [f[0], f[1], ['zh']] : f);
    const h = await harness({ source: aliasSource.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(mixedFonts) + ';'), prefix: 'shared-font-url/' });
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
    const aliasSource = loaderSource(await fixtureText('shared-font-url/index.html'));
    const h = await harness({ source: aliasSource, prefix: 'shared-font-url/' });
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
    const h = await harness(); h.env.omuiFontLanguage = 'en'; h.behavior.fail = 'initial-font:NotoSansSC.ttf';
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
    const required = await harness(); required.env.omuiFontLanguage = 'en'; required.behavior.fail = 'initial-font:NotoSansSC.ttf';
    try { await required.env.omuiLoad.fontBytes('NotoSansSC.ttf'); } catch {}
    required.env.omuiFontLanguage = 'zh'; required.behavior.fail = 'font:NotoSansSC.ttf'; required.start();
    await until(() => required.asset('font:NotoSansSC.ttf')?.state === 'error' && !required.env.omuiFontRecords[required.asset('font:NotoSansSC.ttf').url].transfer.running, 'current required face failure');
    check(required.env.omuiLoad.error?.id === 'font:NotoSansSC.ttf' && required.dependencies.has('font NotoSansSC.ttf') && !required.env.omuiLoad.runtimeInitialized, 'retiring the obsolete face never hides a failure in the newly required face');
    required.behavior.fail = null; await required.env.omuiLoad.retry('font:NotoSansSC.ttf'); await required.ready(); required.present(); await required.complete();
    check(required.env.omuiLoad.phase === 'complete' && required.counts().initialized === 1, 'the current required failure still needs a successful explicit retry before completion');
  });
  await scenario('locale retirement during retry backoff never starts another obsolete transfer', async () => {
    const h = await harness({ holdBackoff: true }); h.env.omuiFontLanguage = 'en';
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
    const initialSource = loaderSource(await fixtureText('all-language-initial/index.html'));
    const h = await harness({ source: initialSource, prefix: 'all-language-initial/' });
    const beforeRequests = (await serverRequests()).length;
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
    check(h.calls.filter(c => c.url === url).length === 1 && (await serverRequests()).slice(beforeRequests).filter(r => 'all-language-initial/' + url === r.name).length === 1, 'DOM plus FS use one actual HTTP request with cache disabled');
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
      const h = await harness(); h.env.omuiFontLanguage = before;
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
    const h = await harness(); h.behavior.fail = 'font:Inter.ttf';
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
    const deferred = await harness({ source: source.replace(/var omuiFonts = [^\n]+;/, 'var omuiFonts = ' + JSON.stringify(noInitial) + ';') });
    let wait; try { await deferred.env.omuiLoad.fontBytes('NotoSansSC.ttf'); } catch (error) { wait = error; }
    check(wait?.code === 'font_deferred' && deferred.calls.length === 0, 'a language-deferred face without initial coverage does not silently fetch before presentation');
  });
  await scenario('real Inter initial coverage works for every language and startup failures remain recoverable', async () => {
    const initialSource = loaderSource(await fixtureText('all-language-initial/index.html'));
    check(!!initialSource, 'the generator supplies a separate real-TTF all-language fixture');
    for (const language of ['en', 'zh']) {
      const h = await harness({ source: initialSource, prefix: 'all-language-initial/', language }); h.env.omuiFontLanguage = language;
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
    const h = await harness({ browserLanguage: 'zh' });
    check(!h.env.omuiLoad.assets.some(a => a.kind === 'font'), 'inline execution does not prematurely choose fonts from browser preference');
    h.env.omuiFontLanguage = 'en'; h.start(); await h.ready();
    check(h.asset('font:NotoSansSC.ttf').group === 'deferred' && h.asset('initial-font:NotoSansSC.ttf').state === 'ready', 'late English bootstrap overrides Chinese browser preference before the first download plan');
    const firstDownload = h.events.find(e => e.assets.some(a => a.state === 'fetching'));
    check(firstDownload.assets.some(a => a.id === 'initial-font:NotoSansSC.ttf' && a.group === 'startup') && firstDownload.assets.some(a => a.id === 'font:NotoSansSC.ttf' && a.group === 'deferred'), 'the first real fetch event already includes the correct startup font denominator');
  });
  await scenario('static language navigation during wasm fetch replans queued fonts before preRun', async () => {
    for (const [before, after] of [['en', 'zh'], ['zh', 'en']]) {
      const h = await harness(); h.env.omuiFontLanguage = before; h.behavior.holdWasm = true; h.start();
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
    const h = await harness({ asks: false }); h.start(); await h.ready(); h.present();
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
    const h = await harness(); h.start(); await h.ready();
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
    check((await serverRequests()).some(r => r.encoded !== r.raw) && h.asset('wasm').loaded === h.asset('wasm').total, 'real HTTP gzip Content-Length is not confused with decoded progress');
  });
  await scenario('four-second fallback is not a presented frame', async () => {
    const h = await harness(); h.start(); await h.ready(); h.timer(4000); h.env.omuiWantFonts(); await h.complete();
    check(!h.env.omuiLoad.presented && !h.events.some(e => e.phase === 'presented') && typeof h.env.omuiFirstFrame === 'function', 'fallback fetches preserve the real native presentation hook');
    const count = h.calls.length; h.present(); h.env.omuiFetchLater();
    check(h.events.filter(e => e.phase === 'presented').length === 1 && h.calls.length === count, 'a later real presentation is reported once without fetching twice');
  });
  await scenario('Chinese opening selects full rather than initial coverage', async () => {
    const h = await harness({ language: 'zh' }); h.start(); await h.ready();
    check(h.asset('font:NotoSansSC.ttf').group === 'startup' && h.asset('initial-font:NotoSansSC.ttf').state === 'skipped', 'a Chinese visit installs full CJK before initialization');
    check(!h.calls.some(c => c.url === h.asset('initial-font:NotoSansSC.ttf').url), 'unused initial coverage does not fetch or inflate the denominator');
  });
  await scenario('no ReadableStream has an honest buffered fallback', async () => {
    const h = await harness({ noStream: true }); h.start(); await h.ready();
    check(h.counts().streamed === 0 && h.counts().buffered === 1 && h.env.omuiLoad.startup.ratio === 1, 'buffered fallback initializes once and counts only actual completed bytes');
  });
  await scenario('unknown totals remain indeterminate until actual EOF', async () => {
    const h = await harness({ unknown: true });
    check(h.env.omuiLoad.total === null && h.env.omuiLoad.ratio === null && h.env.omuiLoad.startup.ratio === null, 'no metadata means null denominators, not a guessed percentage');
    const initial = h.env.omuiLoad.loaded; h.env.Module.setStatus('Downloading (99/100)');
    check(h.env.omuiLoad.loaded === initial && h.env.omuiLoad.ratio === null, 'Module.setStatus text is not parsed into progress');
    h.start(); await h.ready(); check(h.env.omuiLoad.startup.ratio === 1 && h.env.omuiLoad.ratio === null, 'EOF establishes actual startup sizes while deferred totals stay unknown');
    h.present(); await h.complete(); check(h.env.omuiLoad.ratio === 1, 'all actual EOF sizes eventually give a factual complete ratio');
  });
  await scenario('startup font failure retains dependency and can retry without reinitialization', async () => {
    const h = await harness(); h.behavior.fail = 'font:Inter.ttf'; h.start();
    await until(() => h.asset('font:Inter.ttf').state === 'error', 'startup font error');
    check(!h.env.omuiLoad.runtimeInitialized && h.dependencies.has('font Inter.ttf') && !h.files.has('/fonts/Inter.ttf'), 'failed startup font cannot release a false-ready dependency');
    check(h.asset('font:Inter.ttf').attempt === 1 && h.env.omuiLoad.error.retryable, 'HTTP 404 is explicit and does not auto-retry forever');
    await sleep(5); h.behavior.fail = null; await h.env.omuiLoad.retry('font:Inter.ttf'); await h.ready();
    check(h.counts().receives === 1 && h.counts().initialized === 1 && h.dependencies.size === 0, 'explicit font retry completes the original waiting initialization');
  });
  await scenario('failed deferred font preserves partial and omits ready until retry', async () => {
    const h = await harness(); h.start(); await h.ready(); h.behavior.fail = 'font:NotoSansSC.ttf'; h.present();
    await until(() => h.asset('font:NotoSansSC.ttf').state === 'error', 'deferred font error');
    check(h.files.has('/fonts/.partial') && !h.files.has('/later/.ready') && h.env.omuiLoad.presented, 'background failure neither lies about readiness nor restarts the running program');
    await sleep(5); h.behavior.fail = null; await h.env.omuiLoad.retry('font:NotoSansSC.ttf'); await h.complete();
    check(!h.files.has('/fonts/.partial') && h.files.has('/later/.ready') && h.counts().initialized === 1, 'deferred retry installs once and then reports readiness');
  });
  await scenario('interrupted wasm streams retry a bounded number and remain explicitly recoverable', async () => {
    const h = await harness(); h.behavior.fail = 'wasm'; h.behavior.interrupt = true; h.start();
    await until(() => h.asset('wasm').attempt === 3 && !h.env.omuiJobs.wasm.running, 'bounded interrupted fetches');
    check(h.asset('wasm').state === 'error' && h.counts().receives === 0 && !h.env.omuiLoad.runtimeInitialized, 'three interrupted attempts fail visibly without starting main');
    h.behavior.fail = null; await h.env.omuiLoad.retry('wasm'); await h.ready();
    check(h.counts().receives === 1 && h.counts().initialized === 1, 'explicit wasm retry recovers the original hook exactly once');
  });
  await scenario('a silent connection has a bounded inactivity failure', async () => {
    const h = await harness(); h.behavior.fail = 'wasm'; h.behavior.hang = true; h.start();
    for (let n = 1; n <= 3; ++n) { await until(() => h.asset('wasm').attempt === n, 'timeout attempt'); h.timer(15000); }
    await until(() => !h.env.omuiJobs.wasm.running, 'final inactivity error');
    check(h.asset('wasm').state === 'error' && h.asset('wasm').attempt === 3, 'inactivity cannot leave an unreported permanent spinner');
  });
  await scenario('fatal runtime or launcher failures require reload rather than another main', async () => {
    const h = await harness(); h.start(); await h.ready(); const calls = h.calls.length;
    h.env.Module.onAbort('Runtime failed'); const retry = await h.env.omuiLoad.retry();
    check(h.env.omuiLoad.phase === 'error' && retry.reason === 'reload_required' && h.calls.length === calls && h.counts().initialized === 1, 'runtime abort cannot duplicate program initialization');
    const launcher = await harness(); launcher.env.dispatchEvent({ type: 'error', target: { src: new URL(launcher.env.omuiLauncher, base).href } });
    check(launcher.env.omuiLoad.phase === 'error' && launcher.env.omuiLoad.error.id === 'launcher', 'launcher network failure leaves a readable explicit error state');
  });
for (const h of instances) h.dispose();
report.ok = report.checks.length > 0 && report.checks.every(c => c.ok);
return report;
} catch (error) { return { error: String(error.stack || error) }; }
})()
)js";

}  // namespace

int visitor_path(const fs::path& root, const fs::path& built, const fs::path& given_omui,
                 std::string_view url, bool allow_local) {
    const auto chrome = browser::chromium();
    if (!chrome)
        return failed("no headless Chromium for the visitor's path");
    // The site served here as Pages serves it, or a deployed one (`--url`): an https page reaching the bridge on
    // the loopback is what browsers ask the person about (local network access), so that path is checked where
    // it is deployed.
    const bool deployed = !url.empty();
    const fs::path site_dir =
        built.empty() ? root / "target/site" : (built.is_absolute() ? built : root / built);
    if (!deployed && !fs::is_regular_file(site_dir / "index.html"))
        return failed("no target/site (omui-devtools site build first)");
    fs::path omui = given_omui;
    if (omui.empty()) {
        const auto artifacts = run::build(root, {"-p", "omui.cli"});
        if (!artifacts)
            return failed(std::format("omui.cli did not build: {}", artifacts.error()));
        for (const auto& artifact : *artifacts)
            if (artifact.name == "omui" && artifact.kind == "bin")
                omui = artifact.path;
        if (omui.empty())
            return failed("the build of omui.cli reported no omui program");
    }
    cdp::Script script;
    std::error_code error;
    const fs::path bridge_dir =
        fs::temp_directory_path() / std::format("omui-visitor-{:08x}", std::random_device{}());
    fs::create_directories(bridge_dir, error);
    const auto step = [&](bool ok, std::string_view what, std::string detail = {}) {
        return script.check(ok, what, detail.empty() ? std::nullopt : std::optional<json>(detail));
    };
    struct Answer {
        int code = -1;
        std::string out, err;
    };
    // An agent's command: the bridge's directory its own (no person's bridge on this machine is
    // touched).
    const auto page_command = [&](std::vector<std::string> args) {
        namespace process = mcpplibs::os::process;
        process::spawn_options options;
        options.program = omui;
        options.args = {"page"};
        options.args.insert(options.args.end(), args.begin(), args.end());
        options.args.insert(options.args.end(), {"--dir", mcpplibs::os::fs::utf8(bridge_dir)});
        options.in = process::stdio::null;
        auto done =
            process::run(std::move(options), process::bounds{.hard = std::chrono::minutes(10)});
        if (!done)
            return Answer{-1, "", done.error().message()};
        return Answer{done->timed_out ? -1 : done->status.shell_code(), std::move(done->out),
                      std::move(done->err)};
    };
    std::optional<decltype(cdp::Server::start(cdp::pages(site_dir)))> served;
    if (!deployed) {
        served.emplace(cdp::Server::start(cdp::pages(site_dir)));
        if (!*served)
            return failed(served->error());
    }
    const std::string address = deployed ? std::string(url) : (**served)->url();
    // A document's text from where the page is: the loopback server directly, a deployed site through the page's
    // own fetch (same origin).
    const auto fetched = [&](const std::string& from) -> std::string {
        if (!deployed)
            return cdp::get(from).value_or("");
        const json got = script.value(std::format(
            "fetch({}).then(r => r.ok ? r.text() : '').catch(() => '')", json(from).dump()));
        return got.is_string() ? got.get<std::string>() : std::string();
    };
    auto launched = pages::open({.root = root, .site = site_dir, .chrome = *chrome}, 1440, 900);
    if (!launched)
        return failed(launched.error());
    auto& page = **launched;
    script.page = &page;
    std::optional<mcpplibs::os::process::process> bridge;
    std::jthread bridge_reader;
    std::string bridge_said;
    script.attempt(
        [&] {
            script.call("Page.navigate", {{"url", address}});
            for (int index = 0; index < 300 && !script.thrown(); ++index) {
                if (truthy(script.value("!!window.omui && "
                                        "(document.querySelector('[data-omui=mirror]')?."
                                        "childElementCount ?? 0) > 0")))
                    break;
                script.sleep(100);
            }
            step(truthy(script.value("!!window.omui")), "the page runs: window.omui answers");

            // 1. The person's presses (real clicks): the agent panel, then "Copy the prompt" — a
            // pairing code, and the page's agent.md.
            const auto node = [&](const std::string& id) -> json {
                const json answer =
                    script.value("window.omui.snapshot({root: " + q(id) +
                                 ", depth: 0}).then((r) => JSON.stringify(r.data))");
                const json data =
                    json::parse(answer.is_string() ? answer.get<std::string>() : std::string("{}"),
                                nullptr, false);
                for (const auto& found : at(data, "nodes"))
                    if (text(at(found, "id")) == id || text(at(found, "id")).ends_with(id))
                        return found;
                return nullptr;
            };
            const auto press = [&](const std::string& id) {
                const json found = node(id);
                if (!truthy(at(found, "bounds")))
                    return false;
                const json& bounds = at(found, "bounds");
                const double x = number(at(bounds, 0)), y = number(at(bounds, 1)),
                             w = number(at(bounds, 2)), h = number(at(bounds, 3));
                for (const std::string type : {"mouseMoved", "mousePressed", "mouseReleased"})
                    script.call("Input.dispatchMouseEvent", {{"type", type},
                                                             {"x", x + w / 2},
                                                             {"y", y + h / 2},
                                                             {"button", "left"},
                                                             {"clickCount", 1}});
                script.sleep(600);
                return true;
            };
            step(press("main/bar/guide_toggle"), "the person opens the agent panel");
            if (allow_local) {
                // The person answers the browser's question: this page may reach this machine (local network
                // access). Without it a deployed page's requests to the bridge are the browser's to refuse.
                const std::size_t host = address.find("://");
                const std::string origin =
                    address.substr(0, address.find('/', host == std::string::npos ? 0 : host + 3));
                script.call("Browser.grantPermissions",
                            {{"origin", origin}, {"permissions", {"localNetworkAccess"}}});
            }
            step(press("main/body/guide/copy_prompt"), "the person presses \"Copy the prompt\"");
            const json prompt_node = node("main/body/guide/prompt");
            const std::string prompt =
                at(prompt_node, "name").is_string() ? text(at(prompt_node, "name")) : std::string();
            static const std::regex code_form("[A-Z2-9]{4}-[A-Z2-9]{4}"),
                guide_form(R"(https?://\S+agent\.md)");
            std::smatch found;
            const std::string code =
                std::regex_search(prompt, found, code_form) ? found.str() : std::string();
            const std::string guide_url =
                std::regex_search(prompt, found, guide_form) ? found.str() : std::string();
            step(!code.empty() && std::regex_match(code, code_form),
                 "the prompt gives a pairing code", prompt.substr(0, 300));
            step(text(at(script.value("window.omui.linkState()"), "state")) == "waiting",
                 "the page waits for its agent");
            // What the prompt points the agent at: the page's agent.md, and its twin for a program.
            const std::string guide = guide_url.empty() ? std::string() : fetched(guide_url);
            json card;
            if (!guide_url.empty()) {
                const std::string card_url = guide_url.substr(0, guide_url.rfind('/') + 1) +
                                             ".well-known/openmui-agent.json";
                card = json::parse(fetched(card_url), nullptr, false);
                if (card.is_discarded())
                    card = nullptr;
            }
            step(guide.contains("omui connect --code") && truthy(dig(card, "check", "activate")),
                 "the agent reads the page's agent.md",
                 guide_url.empty() ? "no address in the prompt" : guide_url);
            if (script.thrown())
                return;

            // 2. The agent, as the prompt tells it: the bridge with the code, then waiting for the
            // page.
            {
                namespace process = mcpplibs::os::process;
                process::spawn_options options;
                options.program = omui;
                options.args = {"connect",
                                "--code",
                                code,
                                "--name",
                                "CI agent",
                                "--dir",
                                mcpplibs::os::fs::utf8(bridge_dir)};
                options.in = process::stdio::null;
                options.out = process::stdio::pipe;
                options.err = process::stdio::output;
                auto started = process::process::spawn(options);
                if (!started)
                    return script.raise(
                        std::format("omui connect did not start: {}", started.error().message()));
                bridge = std::move(*started);
                bridge_reader = std::jthread([&] {
                    for (;;) {
                        auto chunk = bridge->read_output();
                        if (!chunk || chunk->empty())
                            break;
                        bridge_said += *chunk;
                    }
                });
            }
            const Answer status = page_command({"status", "--wait"});
            step(status.code == 0 && status.out.contains("\"paired\":true"),
                 "the agent sees the page paired (omui page status --wait)",
                 (status.out + status.err).substr(0, 400));
            for (int index = 0;
                 index < 50 &&
                 text(at(script.value("window.omui.linkState()"), "state")) != "paired" &&
                 !script.thrown();
                 ++index)
                script.sleep(100);
            const json state = script.value("window.omui.linkState()");
            step(text(at(state, "state")) == "paired" && text(at(state, "agent")) == "CI agent",
                 "the page sees its agent, by name",
                 state.is_null() ? "undefined" : cdp::stringify(state));
            // The page describes itself to the agent (D-186).
            const Answer described = page_command({"describe"});
            step(described.code == 0 && described.out.size() > 40,
                 "the page describes itself to the agent (omui page describe)",
                 (described.out + described.err).substr(0, 400));

            // 3. The self-check agent.md gives, then the person's log shows what the agent did.
            if (!card.is_object())
                return script.raise("TypeError: Cannot read properties of null (reading 'check')");
            const std::string activate = text(dig(card, "check", "activate")),
                              read_root = text(dig(card, "check", "read")),
                              expect = text(dig(card, "check", "expect"));
            const Answer hello = page_command({"act", activate, "activate"});
            const Answer read = page_command({"snapshot", "--root", read_root, "--depth", "0"});
            step(hello.code == 0 && read.out.contains(expect),
                 std::format("the self-check: {}, then \"{}\"", activate, expect),
                 (hello.out + read.out).substr(0, 400));
            const json log_node = node("main/body/guide/log");
            const json log =
                at(log_node, "value").is_null() ? json::object() : at(log_node, "value");
            step(text(at(log, "link")) == "paired" && number(at(log, "records")) >= 1,
                 "the person's log: the link, and the agent's action", cdp::stringify(log));

            // 4. The agent operates the page through its semantic tree.
            const Answer snap = page_command({"snapshot", "--interactive"});
            step(snap.code == 0 && snap.out.contains("main/bar/nav"),
                 "the agent reads the page (omui page snapshot)", snap.err.substr(0, 300));
            const Answer act = page_command({"act", "main/bar/nav/to_docs", "activate"});
            step(act.code == 0, "the agent acts on it (omui page act … activate)",
                 (act.out + act.err).substr(0, 300));
            script.sleep(1500);
            // Where it is, as the agent reads it: the documentation overview on screen, home's gone
            // from the tree.
            const Answer there = page_command({"snapshot"});
            step(there.out.contains("\"main/body/view/docs") &&
                     !there.out.contains("\"main/body/view/home"),
                 "the page went where the agent took it", there.err.substr(0, 300));
            const Answer back = page_command({"act", "main/bar/mark", "activate"});
            step(back.code == 0, "and back home", (back.out + back.err).substr(0, 300));
            script.sleep(1000);

            // 4. Every control the agent can operate, operated through the page (the interaction
            // sweep, D-91).
            const Answer sweep = page_command({"sweep"});
            std::string summary, failures;
            for (auto part : std::views::split(std::string_view(sweep.out), '\n')) {
                const std::string_view line(part.begin(), part.end());
                if (line.contains("\"nodes\""))
                    summary = std::string(line);
                if (line.contains("\"verdict\":\"failed\""))
                    failures += (failures.empty() ? "" : " ") + std::string(line);
            }
            step(sweep.code == 0, "the interaction sweep through the page (omui page sweep)",
                 summary + (failures.empty() ? "" : " " + failures.substr(0, 800)));
            const json summary_value =
                json::parse(summary.empty() ? "{}" : summary, nullptr, false);
            const double protected_actions =
                summary.empty() ? 0
                                : (dig(summary_value, "data", "person").is_null()
                                       ? 0
                                       : number(dig(summary_value, "data", "person")));
            step(protected_actions >= 2,
                 "the link controls refuse the agent and keep their state (S-10)",
                 std::format("{} protected actions checked", cdp::js_number(protected_actions)));

            // 5. The bridge stops: the page knows.
            bridge->kill();
            static_cast<void>(bridge->wait());
            if (bridge_reader.joinable())
                bridge_reader.join();
            bridge.reset();
            json after;
            for (int index = 0; index < 100 && !script.thrown(); ++index) {
                after = script.value("window.omui.linkState()");
                if (text(at(after, "state")) != "paired")
                    break;
                script.sleep(100);
            }
            step(!after.is_null() && text(at(after, "state")) != "paired",
                 "the bridge gone: the page says so",
                 after.is_null() ? "undefined" : cdp::stringify(after));
        },
        [&](const std::string& message) {
            step(false, "the path ran to its end", "Error: " + message);
        });
    if (bridge) {
        bridge->kill();
        static_cast<void>(bridge->wait());
    }
    if (bridge_reader.joinable())
        bridge_reader.join();
    page.close();
    if (served)
        (**served)->close();
    fs::remove_all(bridge_dir, error);
    json report = {{"steps", script.report["checks"]}, {"ok", script.all_ok()}, {"url", address},
                   {"allowed_local", allow_local}};
    write_json(root / "target/visitor-path.json", report, 1);
    std::cout << "the visitor's path:\n";
    for (const auto& one : report["steps"]) {
        const bool ok = truthy(at(one, "ok"));
        const std::string detail = text(at(one, "detail"));
        std::cout << std::format("  {} {}{}\n", ok ? "ok  " : "FAIL", text(at(one, "what")),
                                 !ok && !detail.empty() ? " — " + detail.substr(0, 300) : "");
    }
    if (!truthy(report["ok"]))
        return failed("the visitor's path failed (target/visitor-path.json)");
    return 0;
}

int dist_toy_loading(const fs::path& root, const fs::path& given_fixture) {
    fs::path fixture = given_fixture;
    if (fixture.empty()) {
        // The fixture is what omui.gen's dist_toy test writes (its scratch directory,
        // dist-toy-loading).
        const auto [status, said] = mcpp(root, {"test", "dist_toy", "-p", "omui.gen"});
        std::cout << said;
        if (status != 0)
            return failed("omui.gen's dist_toy tests failed");
        fixture = fs::temp_directory_path() / "openmui-tests" / "dist-toy-loading" / "index.html";
    }
    const auto chrome = browser::chromium();
    if (!chrome)
        return failed("no headless Chromium for the generated loader's checks");
    if (!fs::is_regular_file(fixture))
        return failed(std::format("no fixture {} (omui.gen's dist_toy tests write it)",
                                  mcpplibs::os::fs::utf8(fixture)));
    const fs::path directory = fixture.parent_path();
    struct Logged {
        std::mutex lock;
        json requests = json::array();
    };
    auto logged = std::make_shared<Logged>();
    auto server = cdp::Server::start([directory,
                                      logged](const cdp::Arrived& request) -> cdp::Reply {
        if (request.path == "/__omui/requests") {
            const std::scoped_lock hold(logged->lock);
            return {.status = 200,
                    .body = logged->requests.dump(),
                    .type = "application/json",
                    .headers = {{"Cache-Control", "no-store"}}};
        }
        if (request.path == "/__omui/harness")
            return {.status = 200,
                    .body = "<!doctype html><meta "
                            "charset=\"utf-8\"><title>dist-toy-loading</title><body></body>",
                    .type = "text/html"};
        if (request.query.contains("omui-blank"))
            return {.status = 200,
                    .body = "<!doctype html><meta charset=\"utf-8\"><body></body>",
                    .type = "text/html"};
        const std::string name =
            mlibs::http::decoded(request.path).substr(request.path.starts_with("/") ? 1 : 0);
        if (name.contains(".."))
            return {.status = 400, .body = "", .type = ""};
        const auto bytes = mcpplibs::os::fs::read(directory / mcpplibs::os::fs::from_utf8(name));
        if (!bytes || name.empty())
            return {.status = 404, .body = "", .type = ""};
        std::string encoded = cdp::gzip(*bytes, 6);
        if (!request.query.contains("omui-unlogged")) {
            const std::scoped_lock hold(logged->lock);
            logged->requests.push_back(
                {{"name", name}, {"raw", bytes->size()}, {"encoded", encoded.size()}});
        }
        return {.status = 200,
                .body = std::move(encoded),
                .type = name.ends_with(".wasm") ? "application/wasm" : "application/octet-stream",
                .headers = {{"Content-Encoding", "gzip"}, {"Cache-Control", "no-store"}}};
    });
    if (!server)
        return failed(server.error());
    auto launched = cdp::Browser::launch(*chrome, {});
    if (!launched)
        return failed(launched.error());
    auto& page = **launched;
    json report;
    std::string problem;
    if (const auto navigated =
            page.call("Page.navigate", {{"url", (*server)->url() + "__omui/harness"}});
        !navigated)
        problem = navigated.error();
    for (int attempt = 0; attempt < 100 && problem.empty(); ++attempt) {
        const auto ready = page.value("document.readyState === 'complete' && !!document.body");
        if (!ready)
            problem = ready.error();
        else if (cdp::truthy(*ready))
            break;
        page.pump(std::chrono::milliseconds(50));
    }
    if (problem.empty()) {
        std::string script(loader_harness);
        const std::string label = mcpplibs::os::fs::utf8(fixture);
        script.replace(script.find("@FIXTURE@"), std::string_view("@FIXTURE@").size(), q(label));
        const auto answer = page.value(script);
        if (!answer)
            problem = answer.error();
        else
            report = *answer;
    }
    page.close();
    (*server)->close();
    if (problem.empty() && report.contains("error"))
        problem = text(report["error"]);
    if (!problem.empty())
        return failed(std::format("the generated loader's checks did not run: {}", problem));
    write_json(root / "target/dist-toy-loading.report.xdef.json", report);
    std::cout << "the generated loader (dist-toy):\n";
    for (const auto& one : at(report, "checks"))
        std::cout << std::format("  {} {}\n", truthy(at(one, "ok")) ? "ok  " : "FAIL",
                                 text(at(one, "what")));
    if (!truthy(at(report, "ok")))
        return failed(
            "the generated loader failed its checks (target/dist-toy-loading.report.xdef.json)");
    return 0;
}

}  // namespace omui::devtools::check
