// Built opening pictures, real resource progress and failure recovery in
// Chromium. UI input uses CDP pointer events; runtime snapshots are read-only.
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';

const directory = process.argv[2] || 'target/site';
const catalogue = JSON.parse(await readFile(directory + '/first-frames.assets.omui.json', 'utf8'));
const html = await readFile(directory + '/index.html', 'utf8');
const fonts = JSON.parse(html.match(/var omuiFonts = (\[[^\n]+\]);/)[1]);
const interFace = fonts.find(f => f[0] === 'Inter.ttf');
const inter = interFace[3] || interFace[1];
const server = await serve(directory), report = { checks: [], scenarios: [] };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : { detail }) });
const until = async (b, expression, timeout = 20000) => {
  const end = Date.now() + timeout;
  while (Date.now() < end) { if (await b.value(expression)) return true; await sleep(50); }
  return false;
};
const click = async (b, selector) => {
  const bounds = await b.value(`(()=>{let e=document.querySelector(${JSON.stringify(selector)});if(!e)return null;let r=e.getBoundingClientRect();return [r.x,r.y,r.width,r.height]})()`);
  if (!bounds || bounds[2] <= 0 || bounds[3] <= 0) throw new Error('No visible control: ' + selector);
  const [x,y,w,h] = bounds;
  for (const type of ['mouseMoved','mousePressed','mouseReleased'])
    await b.call('Input.dispatchMouseEvent', {type,x:x+w/2,y:y+h/2,button:'left',clickCount:1});
};
await mkdir('target/site-loading', { recursive: true });
const shot = async (b, name) => {
  const capture = await b.call('Page.captureScreenshot', {format:'png'});
  await writeFile('target/site-loading/' + name + '.png', Buffer.from(capture.result.data, 'base64'));
};

for (const scenario of [
  {name:'english',language:'en',width:1280,height:800,reduced:false},
  {name:'chinese-phone-reduced',language:'zh',width:390,height:844,reduced:true},
  {name:'route-during-loading',language:'en',width:1280,height:800,reduced:false,navigate:true},
  {name:'font-retry',language:'en',width:1280,height:800,reduced:false,retry:true},
  {name:'deduplicated-preview',language:'en',width:1280,height:800,reduced:false,start:'components',dedup:true}
]) {
  const b = await browser(process.env.CHROME, {width:scenario.width,height:scenario.height});
  const log = {...scenario, exceptions:[]}; report.scenarios.push(log);
  let held = null, failFont = !!scenario.retry;
  try {
    b.on((method, params) => {
      if (method === 'Runtime.exceptionThrown') log.exceptions.push(params.exceptionDetails.text);
      if (method !== 'Fetch.requestPaused') return;
      if (failFont && params.request.url.endsWith('/' + inter)) {
        void b.call('Fetch.fulfillRequest', {requestId:params.requestId,responseCode:404,body:''}); return;
      }
      if (!scenario.retry && /\.wasm(?:\?|$)/.test(params.request.url)) { held = params.requestId; return; }
      void b.call('Fetch.continueRequest', {requestId:params.requestId});
    });
    await b.call('Network.enable');
    await b.call('Network.setCacheDisabled', {cacheDisabled:true});
    await b.call('Network.emulateNetworkConditions', {offline:false,latency:40,downloadThroughput:2500000,uploadThroughput:625000});
    await b.call('Fetch.enable', {patterns:[{urlPattern:'*.wasm',requestStage:'Request'},...(scenario.retry?[{urlPattern:'*'+inter,requestStage:'Request'}]:[])]});
    await b.call('Emulation.setEmulatedMedia', {features:[{name:'prefers-reduced-motion',value:scenario.reduced?'reduce':'no-preference'}]});
    await b.call('Page.addScriptToEvaluateOnNewDocument', {source:`
      Object.defineProperty(navigator,'languages',{value:['zh-CN']});
      window.__openingEvents=[];
      addEventListener('omui-load',event=>{
        let d=event.detail; window.__openingEvents.push({phase:d.phase,loaded:d.startup.loaded,total:d.startup.total,ratio:d.startup.ratio});
        if(d.phase==='presented'&&!window.__openingFirst){let c=document.getElementById('canvas'),x=c.getContext('2d');
          if(x)window.__openingFirst={width:c.width,height:c.height,pixels:x.getImageData(0,0,c.width,c.height).data};}
      });`});
    await b.call('Page.navigate', {url:server.url+'?renderer=cpu#/'+(scenario.language==='zh'?'zh/':'')+(scenario.start||'home')});
    const visible = await until(b, `!!window.omuiOpeningVisible && !document.getElementById('omui-static').hidden`);
    check(visible, scenario.name+': native opening is visible while the runtime is loading');
    if (!visible) throw new Error('Opening did not become visible');
    if (!scenario.retry) check(await until(b, `[...document.fonts].some(f=>f.family.replace(/['"]/g,'')==='Inter'&&f.status==='loaded')`), scenario.name+': the opening uses the actual initial Inter face');
    log.static_readable_ms = await b.value('performance.now()');
    await shot(b, scenario.name+'-static');
    log.static_capture_ms = await b.value('performance.now()');
    log.opening = await b.value('window.omuiOpeningVisible');
    log.paint = await b.value(`performance.getEntriesByType('paint').map(x=>({name:x.name,at:x.startTime}))`);
    check(log.opening.language === scenario.language, scenario.name+': the opening follows the selected page language');
    check(await b.value(`(()=>{let s=document.querySelector('.omui-opening')?.closest('section')?.querySelector('.landing-mark>svg');return !s||getComputedStyle(s).display==='none'})()`), scenario.name+': the picture replaces its SVG fallback without duplicating the mark');
    check(await b.value(`(()=>{let n=document.querySelector('nav[data-locale="${scenario.language}"]');return n&&!n.hidden&&getComputedStyle(n).display==='flex'})()`), scenario.name+': the active navigation preserves its flex layout');
    check(await until(b, `!!document.querySelector('.omui-opening[data-nodes]')`), scenario.name+': loading geometry comes from real semantic rectangles');
    const requested = await b.value(`performance.getEntriesByType('resource').map(x=>new URL(x.name).pathname.split('/').pop())`);
    const previews = new Set(catalogue.previews.map(x=>x.image.split('/').pop()));
    const chosen = requested.filter(x=>previews.has(x));
    check(chosen.length === 1, scenario.name+': only the current opening picture is requested', chosen);
    if (!scenario.retry) check(log.static_readable_ms <= 1000, scenario.name+': the picture and initial face are ready within one second at 20 Mbit/s',log.static_readable_ms);
    if (scenario.reduced) {
      check(await b.value(`getComputedStyle(document.querySelector('.omui-opening svg')).display==='none'`), 'reduced motion uses the static native picture without drawing the tree');
      const expected = catalogue.previews.find(x=>x.route==='home'&&x.language==='zh'&&x.viewport[0]===390&&x.motion==='reduced');
      check(log.opening.image===expected.image,'reduced motion selects its matching assembled first frame');
      check(await b.value(`(()=>{let n=document.querySelector('nav[data-locale="zh"]');return !n.hidden&&!n.querySelector('.static-menu').hidden&&n.querySelector('.static-links').hidden&&n.getBoundingClientRect().height<55})()`), 'the narrow static header starts with one collapsed menu row');
      await click(b,'nav[data-locale="zh"] .static-menu');
      check(await b.value(`(()=>{let n=document.querySelector('nav[data-locale="zh"]');return n.querySelector('.static-menu').getAttribute('aria-expanded')==='true'&&!n.querySelector('.static-links').hidden})()`), 'the static phone menu opens through actual pointer input');
      await click(b,'nav[data-locale="zh"] .static-menu');
      check(await b.value(`document.querySelector('nav[data-locale="zh"] .static-links').hidden`), 'the static phone menu closes before live handoff');
    }
    if (scenario.retry) {
      const failed = await until(b, `['font:Inter.ttf','initial-font:Inter.ttf'].includes(window.omuiLoad?.error?.id)`, 30000);
      check(failed, 'a failed required font produces an explicit recoverable state');
      check(await b.value(`!window.omuiStaticHandedOff && !document.getElementById('omui-static').hidden && !document.querySelector('.omui-loading-note button').hidden`), 'font failure keeps the readable page and a visible retry action');
      failFont = false;
      await click(b,'.omui-loading-note button');
    } else {
      check(!!held, scenario.name+': wasm is genuinely held before presentation');
      check(await b.value(`!window.omuiHasPresented && !window.omuiStaticHandedOff`), scenario.name+': the picture does not pretend to be the live UI');
      const fontPlan = await b.value(`window.omuiLoad.assets.find(x=>x.id==='font:NotoSansSC.ttf')?.group`);
      check(fontPlan === (scenario.language==='zh'?'startup':'deferred'), scenario.name+': the chosen page overrides browser language for startup fonts',fontPlan);
      if (scenario.navigate) {
        await click(b,'nav[data-locale="en"] a[href="#/motion"]');
        check(await until(b, `window.omuiOpeningVisible?.route==='motion'`), 'static navigation selects the new page picture before wasm is available');
        const count = await b.value(`performance.getEntriesByType('resource').map(x=>new URL(x.name).pathname.split('/').pop())`);
        check(count.filter(x=>previews.has(x)).length===2, 'loading navigation fetches only the old and newly selected opening');
      }
      if (scenario.dedup) {
        const previous = await b.value('window.omuiOpeningVisible');
        await b.call('Page.navigate', {url:server.url+'?renderer=cpu#/zh/components'});
        check(await until(b, `window.omuiOpeningVisible?.language==='zh'&&!!document.querySelector('.omui-opening[data-nodes]')`), 'a locale change updates a deduplicated picture and its geometry before wasm loads');
        const translated = await b.value('window.omuiOpeningVisible');
        // Each locale's picture and geometry its own; where two locales' pictures are the same bytes (one
        // deduplicated file), still never the other's geometry. (The components opening, the wall, is
        // translated: its pictures differ — the shared case is exercised where they are the same.)
        check(previous.geometry!==translated.geometry, 'another locale\'s picture brings its own semantic geometry, shared bytes or not',{previous,translated});
        check(await b.value(`document.querySelector('.omui-opening figcaption').textContent==='界面预览 · 加载完成后即可操作'`), 'the reused picture has the selected language\'s caption');
        await b.call('Emulation.setEmulatedMedia', {features:[{name:'prefers-reduced-motion',value:'reduce'}]});
        check(await until(b, `window.omuiOpeningVisible?.motion==='reduced'&&!!document.querySelector('.omui-opening[data-nodes]')`), 'a motion preference change selects new geometry even when the PNG is identical');
        const reduced = await b.value('window.omuiOpeningVisible');
        check(reduced.image===translated.image&&reduced.geometry!==translated.geometry, 'reduced-motion metadata is independent of deduplicated image bytes');
      }
      await b.call('Fetch.continueRequest', {requestId:held}); held=null;
      check(await until(b, `(window.__openingEvents||[]).some(x=>typeof x.ratio==='number'&&x.ratio>0&&x.ratio<1)`), scenario.name+': intermediate decoded bytes drive actual progress');
    }
    const handed = await until(b, `window.omuiStaticHandedOff===true && window.omuiLoad?.presented===true`, 45000);
    check(handed, scenario.name+': presentation and semantic readiness hand over once');
    if (!handed) throw new Error('Runtime did not hand over');
    check(await b.value(`document.getElementById('omui-static').hidden && document.querySelector('[data-omui=mirror]').getAttribute('aria-hidden')!=='true'`), scenario.name+': only the live accessible surface remains');
    if (scenario.navigate) {
      check(await b.value(`!!document.querySelector('[data-omui-id="main/body/view/explore/page/works/grid/mark"]')`), 'the live handoff follows the page chosen during loading');
    } else if (scenario.dedup) {
      const finalLocale = await b.value(`({root:!!document.querySelector('[data-omui-id="main/body/view/components/page/gallery/detail/button/stage/specimen"]'),language:document.documentElement.lang,fontPlan:window.omuiLoad.assets.find(x=>x.id==='font:NotoSansSC.ttf')?.group,fontLanguage:window.omuiFontLanguage})`);
      check(finalLocale?.root&&finalLocale.language==='zh-CN'&&finalLocale.fontPlan==='startup', 'the locale selected while loading reaches the live component and its startup font plan',finalLocale);
    } else if (!scenario.retry) {
      const reference = catalogue.references.find(x=>x.language===scenario.language&&x.viewport[0]===scenario.width&&x.motion===(scenario.reduced?'reduced':'full'));
      const result = await b.value(`(async()=>{
        let actual=window.__openingFirst;if(!actual)return {error:'first CPU pixels not captured'};
        let image=new Image(); image.src=${JSON.stringify(reference.image)};await image.decode();
        let c=document.createElement('canvas');c.width=image.naturalWidth;c.height=image.naturalHeight;
        let x=c.getContext('2d');x.drawImage(image,0,0);let wanted=x.getImageData(0,0,c.width,c.height).data;
        let differences=0,first=-1;for(let i=0;i<wanted.length;i++)if(wanted[i]!==actual.pixels[i]){differences++;if(first<0)first=i;}
        return {width:actual.width,height:actual.height,expectedWidth:c.width,expectedHeight:c.height,differences,first};
      })()`);
      check(result && result.differences===0&&result.width===result.expectedWidth&&result.height===result.expectedHeight, scenario.name+': the first presented CPU frame matches the build-time full reference pixel for pixel',result);
      if (scenario.name==='english') {
        const glyphs = await b.value(`(async()=>{
          let f=omuiFonts.find(x=>x[0]==='Inter.ttf');if(!f[3])return {error:'initial Inter asset missing'};
          let full=new FontFace('omui-check-full','url('+f[1]+')',{weight:'100 900'}),small=new FontFace('omui-check-initial','url('+f[3]+')',{weight:'100 900'});
          await Promise.all([full.load(),small.load()]);document.fonts.add(full);document.fonts.add(small);
          let c=document.createElement('canvas');c.width=512;c.height=80;let x=c.getContext('2d'),wrong=[];
          for(let weight of [400,600])for(let text of ['Office','Notification','configuration','One interface.','Every perspective.',':-)','UI (CPU)','A-B','2x3']){
            let draw=family=>{x.fillStyle='#fff';x.fillRect(0,0,512,80);x.font=weight+' 28px '+family;x.fillStyle='#16161a';x.fillText(text,4,55);return x.getImageData(0,0,512,80).data;};
            let a=draw('omui-check-full'),b=draw('omui-check-initial');if(a.some((v,i)=>v!==b[i]))wrong.push({text,weight});
          }return {differences:wrong};
        })()`);
        check(glyphs && glyphs.differences?.length===0,'initial Inter preserves UI glyphs and actual contextual substitutions at both weights',glyphs);
      }
    }
    check(log.exceptions.length===0,scenario.name+': no unexpected browser runtime exceptions',log.exceptions);
    await shot(b,scenario.name+'-live');
  } catch (error) { check(false,scenario.name+': scenario completes',String(error.stack||error)); }
  finally { if(held)await b.call('Fetch.continueRequest',{requestId:held}); b.close(); }
}
server.close();
report.ok=report.checks.every(x=>x.ok);
console.log(JSON.stringify(report,null,2));
process.exit(report.ok?0:1);
