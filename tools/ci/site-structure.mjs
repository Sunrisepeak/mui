// Information architecture and the static-to-program handoff in Chromium.
import { browser, serve, sleep } from './page.mjs';
const server = await serve(process.argv[2] || 'target/site');
const report = { checks: [] };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail ? { detail } : {}) });
const b = await browser(process.env.CHROME, { width: 1280, height: 800 });
const until = async (expression, timeout = 30000) => {
  for (let i = 0; i < timeout / 100; i++) { if (await b.value(expression)) return true; await sleep(100); }
  return false;
};
try {
  await b.call('Emulation.setScriptExecutionDisabled', { value: true });
  for (const [route, expected, lang] of [['docs/features', 'features', 'en'], ['zh/brand', 'brand', 'zh-CN'], ['explore', 'explore', 'en']]) {
    await b.call('Page.navigate', { url: server.url + '#/' + route }); await sleep(400);
    const result = await b.value(`(() => {
      const root=document.getElementById('omui-static');
      return Array.from(root.querySelectorAll('section[data-place]')).filter(n=>getComputedStyle(n).display!=='none').map(n=>({place:n.dataset.place,lang:n.lang}));
    })()`);
    check(result?.length === 1 && result[0].place === expected && result[0].lang === lang, 'without JavaScript: ' + route + ' shows only its own content', result);
    const ownFont = await b.value(`document.fonts.ready.then(()=>[...document.fonts].some(f=>f.family.replace(/['"]/g,'')==='Inter'&&f.status==='loaded'))`);
    check(ownFont, 'without JavaScript: ' + route + ' loads its own body font through noscript');
  }
  await b.call('Emulation.setScriptExecutionDisabled', { value: false });
  // Hold wasm itself; the real static links must work throughout this wait.
  let held;
  b.on((method, params) => { if (method === 'Fetch.requestPaused') held = params.requestId; });
  await b.call('Fetch.enable', { patterns: [{ urlPattern: '*.wasm*', requestStage: 'Request' }] });
  await b.call('Page.navigate', { url: server.url });
  for (let i = 0; !held && i < 100; i++) await sleep(100);
  check(held && await b.value(`!document.getElementById('omui-static').hidden && !window.omuiHasPresented`), 'static content stays visible while wasm is pending');
  const bounds = await b.value(`(() => { const a=document.querySelector('#omui-static nav[data-locale=en] a[data-omui-target="main/bar/nav/to_docs"]'); const r=a.getBoundingClientRect(); return {x:r.x+r.width/2,y:r.y+r.height/2}; })()`);
  if (bounds) for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased'])
    await b.call('Input.dispatchMouseEvent', { type, ...bounds, button: 'left', clickCount: 1 });
  await sleep(300);
  check(await b.value(`location.hash==='#/docs' && !document.querySelector('#omui-static section[data-place=docs][lang=en]').hidden`), 'a real click changes the readable page before wasm starts');
  if (held) await b.call('Fetch.continueRequest', { requestId: held });
  await b.call('Fetch.disable');
  check(await until(`window.omuiStaticHandedOff && document.querySelector('[data-omui-id="main/body/view/docs/page/read/search"]')`), 'handoff waits for the requested live page and a presented frame');
  const state = await b.value(`({ staticHidden:document.getElementById('omui-static').hidden,
    mirrorInert:document.querySelector('[data-omui=mirror]').inert,
    focus:document.activeElement.getAttribute('data-omui-id') })`);
  check(state?.staticHidden && !state.mirrorInert && state.focus === 'main/bar/nav/to_docs', 'handoff has one accessible surface and preserves the focused navigation', state);
  const origin = await b.value('performance.timeOrigin');
  for (const [route, place] of [['zh/docs/features', 'features'], ['zh/brand', 'brand'], ['zh/missing-page', 'not_found']]) {
    await b.value(`location.hash=${JSON.stringify('/' + route)}; true`);
    check(await until(`document.querySelector('[data-omui-id="main/body/view/${place}/page"]')`), 'the live route opens ' + route);
  }
  await b.value('history.back(); true');
  check(await until(`location.hash==='#/zh/brand' && document.querySelector('[data-omui-id="main/body/view/brand/page"]')`), 'Back restores an alias without replacing the history entry');
  await b.value('history.forward(); true');
  check(await until(`location.hash==='#/zh/missing-page' && document.querySelector('[data-omui-id="main/body/view/not_found/page"]')`), 'Forward restores the missing-page state');
  check(await b.value('performance.timeOrigin') === origin, 'history navigation keeps the same document and runtime');
  await b.value(`window.omui.act('main/bar/lang','activate')`);
  check(await until(`location.hash==='#/missing-page' && document.querySelector('[data-omui-id="main/body/view/not_found/page/missing/title"]')?.getAttribute("aria-label")==="This page isn't here."`), 'changing the missing page language keeps its address and content consistent');
  held = null;
  await b.call('Network.enable');
  await b.call('Network.setCacheDisabled', { cacheDisabled: true });
  await b.call('Fetch.enable', { patterns: [{ urlPattern: '*.wasm*', requestStage: 'Request' }] });
  await b.call('Page.navigate', { url: server.url + '?handoff-scroll=1#/docs/features' });
  for (let i = 0; !held && i < 100; i++) await sleep(100);
  await until(`!window.omuiStaticHandedOff && document.querySelector('#omui-static section[data-place=features][lang=en]')?.hidden===false`, 10000);
  await b.value('document.fonts.ready.then(() => true)');
  await b.value(`document.getElementById('omui-static').scrollTop=360; true`);
  const scrolled = await b.value(`document.getElementById('omui-static').scrollTop`);
  if (held) await b.call('Fetch.continueRequest', { requestId: held });
  await b.call('Fetch.disable');
  await until('window.omuiStaticHandedOff');
  const restored = await b.value(`window.omui.snapshot({root:'main/body/view/features',depth:0}).then(r => (r.data?.nodes ?? [])[0]?.value?.y)`);
  check(scrolled > 100 && Math.abs(restored - scrolled) < 1, 'a reader scrolling during loading keeps the scroll offset on handoff', { scrolled, restored });
} catch (error) { check(false, 'browser scenario completed', String(error.stack || error)); }
finally { b.close(); server.close(); }
report.ok = report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2));
process.exit(report.ok ? 0 : 1);
