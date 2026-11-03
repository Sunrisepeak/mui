// Hold the full Chinese font to verify the initial glyphs, then compare real pixels.
import { readFileSync, writeFileSync, mkdirSync, statSync } from 'node:fs';
import { browser, serve, sleep } from './page.mjs';
const dir = process.argv[2] || 'target/site';
const fonts = JSON.parse(readFileSync(`${dir}/index.html`, 'utf8').match(/var omuiFonts = ([^\n]+);/)[1]);
const chinese = fonts.find(f => f[0] === 'NotoSansSC.ttf');
const report = { checks: [], assets: { full: chinese?.[1], initial: chinese?.[3] } };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail ? { detail } : {}) });
check(chinese?.[2]?.includes('zh') && chinese?.[3], 'the Chinese face has language selection and initial coverage');
if (!chinese?.[3]) { console.log(JSON.stringify(report, null, 2)); process.exit(1); }
report.assets.initial_bytes = statSync(`${dir}/${chinese[3]}`).size;
report.assets.full_bytes = statSync(`${dir}/${chinese[1]}`).size;
const pinned = JSON.parse(readFileSync('site/reference/ci.frames.omui.json', 'utf8')).frames;
const server = await serve(dir);
mkdirSync('target/site-fonts', { recursive: true });
let switchedBody;
for (const c of [
  { name: 'english', route: '', width: 1280 },
  { name: 'chinese', route: 'zh', width: 1280, full: true },
  { name: 'english-chinese-browser', route: '', width: 1280, locale: 'zh-CN' },
  { name: 'english-phone', route: '', width: 390 }
]) {
  const b = await browser(process.env.CHROME, { width: c.width, height: c.width === 390 ? 844 : 800 });
  let held; const requests = [];
  b.on((method, p) => {
    if (method === 'Fetch.requestPaused') held = p.requestId;
    if (method === 'Network.requestWillBeSent') requests.push(p.request.url);
  });
  const until = async (expression, ms = 30000) => {
    for (let i = 0; i < ms / 100; i++) { if (await b.value(expression)) return true; await sleep(100); }
    return false;
  };
  const capture = async (clip, suffix) => {
    const r = await b.call('Page.captureScreenshot', { format: 'png', clip: { ...clip, scale: 1 } });
    const data = r.result?.data;
    if (data) writeFileSync(`target/site-fonts/${c.name}-${suffix}.png`, Buffer.from(data, 'base64'));
    return data;
  };
  const captureHeading = async suffix => {
    const bounds = await b.value(`window.omui.snapshot({root:'main/body/view/home/page/landing/intro/title',depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)`);
    if (!Array.isArray(bounds) || bounds.length !== 4) throw new Error('The actual home heading has no bounds');
    const [x,y,width,height] = bounds;
    return capture({x,y,width,height}, suffix);
  };
  try {
    await b.call('Network.enable');
    let source = `(() => { let value=false; Object.defineProperty(window,'omuiHasPresented',{get:()=>value,set:v=>{value=v;if(v&&!window.__fontPresentedAt)window.__fontPresentedAt=performance.now();},configurable:true}); })();`;
    if (c.locale) source += `Object.defineProperty(navigator,'languages',{get:()=>[${JSON.stringify(c.locale)}]});Object.defineProperty(navigator,'language',{get:()=>${JSON.stringify(c.locale)}});`;
    await b.call('Page.addScriptToEvaluateOnNewDocument', { source });
    if (!c.full) await b.call('Fetch.enable', { patterns: [{ urlPattern: `*${chinese[1]}`, requestStage: 'Request' }] });
    await b.call('Page.navigate', { url: `${server.url}#/${c.route}` });
    check(await until('window.omuiStaticHandedOff'), `${c.name}: the program presents and takes over`);
    const timing = await b.value(`({ frame:window.__fontPresentedAt, language:window.omuiFontLanguage,
      initial:performance.getEntriesByType('resource').find(r=>r.name.endsWith(${JSON.stringify(chinese[3])}))?.responseEnd,
      full:performance.getEntriesByType('resource').find(r=>r.name.endsWith(${JSON.stringify(chinese[1])}))?.responseEnd,
      browserLanguage:navigator.language })`);
    if (c.full) {
      check(timing?.language === 'zh' && timing.full > 0 && timing.full <= timing.frame && !requests.some(r=>r.endsWith(chinese[3])),
        'chinese: the full face arrives before presentation, without a redundant initial download', timing);
      await sleep(500);
      const direct = await captureHeading('body');
      check(direct && direct === switchedBody, 'switching to Chinese after the full face arrives matches a direct Chinese visit');
      continue;
    }
    check(held && timing?.language === 'en' && timing.initial > 0 && timing.initial <= timing.frame && !timing.full,
      `${c.name}: initial glyphs arrive before the first presentation while the full face is held`, timing);
    await sleep(250);
    const node = await b.value(`window.omui.snapshot({root:'main/bar/lang',depth:0}).then(r=>(r.data?.nodes??[]).find(n=>n.id==='main/bar/lang'))`);
    check(node?.name === '中文' && node.bounds?.[2] > 24, `${c.name}: the Chinese language control is present and measurable`);
    const [x, y, width, height] = node.bounds;
    const clip = { x, y, width, height };
    const before = await capture(clip, 'initial');
    const act = async (id, action = 'activate', args) => b.value(`window.omui.act(${JSON.stringify(id)},${JSON.stringify(action)},${JSON.stringify(args ? {args} : {})})`);
    const reference = async () => b.value(`window.omui.snapshot({root:'main/body/check',depth:3}).then(r=>Object.fromEntries((r.data?.nodes??[]).map(n=>[n.id.split('/').pop(),n.name??''])))`);
    if (c.name === 'english') {
      await act('main/foot/tools_toggle'); await act('main/foot/ci_check'); await sleep(250);
      await act('main/body/check/ref_frame','set_value',{value:30});
      await act('main/body/check/ref_frame','set_value',{value:58}); await sleep(250);
      await act('main/foot/ci_check'); await sleep(150); await act('main/foot/ci_check'); await sleep(150);
      const waiting = await reference();
      check(waiting.verdict?.startsWith('Waiting for the reference fonts'), 'reference replay waits for the full font instead of reporting a pixel mismatch', waiting);
    }
    if (held) await b.call('Fetch.continueRequest', { requestId: held });
    await b.call('Fetch.disable');
    await until(`performance.getEntriesByType('resource').some(r=>r.name.endsWith(${JSON.stringify(chinese[1])}))`);
    await sleep(1500);
    const after = await capture(clip, 'complete');
    check(before && before === after, `${c.name}: the two glyphs have identical pixels before and after the full font`);
    if (c.name === 'english') {
      let first;
      for (let i=0;i<200;i++) { first=await reference(); if(first.mine?.endsWith(pinned[58]))break; await sleep(100); }
      check(first.mine?.endsWith(pinned[58]) && first.verdict?.startsWith('✓'), 'font readiness resumes the latest frame after closing and reopening the panel', first);
      let mismatch;
      for(let frame=0;frame<pinned.length;frame++) {
        await act('main/body/check/ref_frame','set_value',{value:frame});
        let result;
        for(let i=0;i<100;i++){result=await reference();if(result.mine?.endsWith(pinned[frame])&&result.ci?.endsWith(pinned[frame])&&result.verdict?.startsWith('✓'))break;await sleep(50);}
        if(!result.mine?.endsWith(pinned[frame])||!result.ci?.endsWith(pinned[frame])||!result.verdict?.startsWith('✓')){mismatch={frame,...result};break;}
      }
      check(!mismatch, 'all 59 browser reference frames match the native CI hashes after deferred font loading', mismatch);
      if(mismatch)await capture({x:0,y:0,width:1280,height:800},'reference-mismatch');
      await act('main/foot/ci_check'); await act('main/foot/tools_toggle');
      await b.value(`window.omui.act('main/bar/lang','activate')`); await sleep(1500);
      switchedBody = await captureHeading('chinese-body');
    }
  } catch (e) { check(false, `${c.name}: scenario completes`, String(e.stack || e)); }
  finally { b.close(); }
}
server.close();
report.ok = report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2));
process.exit(report.ok ? 0 : 1);
