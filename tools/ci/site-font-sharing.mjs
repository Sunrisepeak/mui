// Real Chromium: static FontFace bytes and runtime FS fonts share downloads.
// Run separately from glyph probes that intentionally create URL FontFaces.
// CHROME=<chromium> node tools/ci/site-font-sharing.mjs <built-site> > report.json
import { readFile, mkdtemp, rm, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { browser, serve, sleep } from './page.mjs';

const directory = process.argv[2] || 'target/site';
const html = await readFile(directory + '/index.html', 'utf8');
const fonts = JSON.parse(html.match(/var omuiFonts = (\[[^\n]+\]);/)[1]);
const inter = fonts.find(f => f[0] === 'Inter.ttf');
const report = { checks: [], scenarios: [], cache: 'disabled; fresh Chrome profile per scenario; service workers bypassed',
  bytes: 'encoded_body_bytes sums Network.dataReceived; encoded_transport_bytes is Network.loadingFinished including response headers' };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : {detail}) });
const fontFiles = new Map();
for (const [name, full, languages, initial] of fonts) {
  for (const [url, variant] of [[full, 'full'], ...(initial ? [[initial, 'initial']] : [])]) {
    const bytes = await readFile(directory + '/' + url);
    fontFiles.set(url, { name, variant, length: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') });
  }
}
const families = { 'Inter.ttf': 'Inter', 'JetBrainsMono.ttf': 'JetBrains Mono', 'NotoSansSC.ttf': 'Noto Sans SC' };
const server = await serve(directory);
await mkdir('target/site-font-delivery', {recursive:true});
const until = async (b, expression, timeout = 20000) => {
  const end = Date.now() + timeout;
  while (Date.now() < end) { if (await b.value(expression)) return true; await sleep(50); }
  return false;
};
const scenarios = [
  {name:'english-home', route:'home', width:1280},
  {name:'chinese-home', route:'zh/home', width:1280},
  {name:'english-phone', route:'home', width:390},
  {name:'chinese-phone', route:'zh/home', width:390},
  {name:'documentation-code', route:'docs/start', width:1280},
  {name:'pending-shared-response', route:'home', width:1280, pending:true},
  {name:'runtime-before-static-consumer', route:'home', width:1280, runtimeFirst:true},
  {name:'shared-failure-and-retry', route:'home', width:1280, retry:true}
];
let supported = true;
for (const scenario of scenarios) {
  if (!supported) { report.not_run = scenarios.slice(report.scenarios.length).map(s => s.name); break; }
  const profile = await mkdtemp(join(tmpdir(), 'omui-font-sharing-'));
  const log = {...scenario, requests:[], exceptions:[]}; report.scenarios.push(log);
  let b, wasm = null, fontResponse = null;
  const byId = new Map();
  const initial = inter?.[3] || inter?.[1];
  const count = path => log.requests.filter(r => r.path === path).length;
  const ensure = (ok, what, detail) => { check(ok, scenario.name + ': ' + what, detail); if (!ok) throw new Error(what); };
  try {
    b = await browser(process.env.CHROME, {width:scenario.width, height:scenario.width===390?844:800, flags:['--user-data-dir='+profile]});
    report.browser = b.version;
    b.on((method, p) => {
      if (method === 'Runtime.exceptionThrown') log.exceptions.push(p.exceptionDetails.text);
      if (method === 'Network.requestWillBeSent') {
        const path = new URL(p.request.url).pathname.slice(1);
        if (fontFiles.has(path)) {
          const row = {id:p.requestId,path,url:p.request.url,type:p.type,initiator:p.initiator?.type,encoded_body_bytes:0};
          log.requests.push(row); byId.set(p.requestId,row);
        }
      }
      const row = byId.get(p.requestId);
      if (row && method === 'Network.responseReceived') Object.assign(row,{status:p.response.status,cache:!!p.response.fromDiskCache,service_worker:!!p.response.fromServiceWorker});
      if (row && method === 'Network.requestServedFromCache') row.cache = true;
      if (row && method === 'Network.dataReceived') row.encoded_body_bytes += p.encodedDataLength;
      if (row && method === 'Network.loadingFinished') row.encoded_transport_bytes = p.encodedDataLength;
      if (row && method === 'Network.loadingFailed') row.error = p.errorText;
      if (method !== 'Fetch.requestPaused') return;
      if (/\.wasm(?:\?|$)/.test(p.request.url)) wasm = p.requestId;
      else if (p.request.url.endsWith('/'+initial)) fontResponse = p.requestId;
      else void b.call('Fetch.continueRequest',{requestId:p.requestId});
    });
    await b.call('Network.enable');
    await b.call('Network.setCacheDisabled',{cacheDisabled:true});
    await b.call('Network.setBypassServiceWorker',{bypass:true});
    await b.call('Emulation.setEmulatedMedia',{features:[{name:'prefers-reduced-motion',value:'reduce'}]});
    // Hide only the public protocol until both font consumers can be observed.
    // Native presentation and its deferred-font hook retain their real values.
    // The runtime-first case delays external fontBytes consumers; the original
    // byte API and runtime startup remain real and run without interception.
    await b.call('Page.addScriptToEvaluateOnNewDocument',{source:`(() => {
      let protocol;
      window.__fontSharingHold = true; window.__fontSharingPresented = false;
      Object.defineProperty(window,'omui',{configurable:true,get:()=>window.__fontSharingHold?undefined:protocol,set:value=>{protocol=value;}});
      Object.defineProperty(window,'omuiHasPresented',{configurable:true,get:()=>window.__fontSharingPresented,set:value=>{window.__fontSharingPresented=value;}});
      Object.defineProperty(navigator,'languages',{get:()=>['en-US']});
      if (${!!scenario.runtimeFirst}) {
        let load; window.__fontSharingExternalReady=false; window.__fontSharingExternalWaiters=[];
        Object.defineProperty(window,'omuiLoad',{configurable:true,get:()=>load,set:value=>{
          load=value; if(typeof value?.fontBytes!=='function')return;
          const actual=value.fontBytes.bind(value);
          value.fontBytes=(...args)=>window.__fontSharingExternalReady?actual(...args):new Promise(resolve=>window.__fontSharingExternalWaiters.push(resolve)).then(()=>actual(...args));
        }});
      }
    })();`});
    await b.call('Fetch.enable',{patterns:[{urlPattern:'*.wasm',requestStage:'Request'},
      ...((scenario.pending||scenario.retry)?[{urlPattern:'*'+initial,requestStage:'Response'}]:[])]});
    await b.call('Page.navigate',{url:server.url+'?renderer=cpu#/'+scenario.route});
    supported = await until(b, `typeof window.omuiLoad?.fontBytes==='function' && window.omuiStaticFonts?.supported===true`, 3000);
    ensure(supported, 'the shared fontBytes lease and static-font data contracts are available');
    for (let i=0;!wasm&&i<100;i++) await sleep(50);
    ensure(!!wasm, 'wasm is held before preRun so consumer ordering is controlled');
    const releaseWasm = async () => { const id=wasm; wasm=null; await b.call('Fetch.continueRequest',{requestId:id}); };
    const releaseFont = async () => { const id=fontResponse; fontResponse=null; await b.call('Fetch.continueResponse',{requestId:id}); };
    const staticState = () => b.value(`({state:window.omuiStaticFonts,faces:[...document.fonts].map(f=>({family:f.family.replace(/['"]/g,''),status:f.status})),hidden:document.getElementById('omui-static').hidden,handed:!!window.omuiStaticHandedOff,assets:window.omuiLoad.assets})`);

    if (scenario.runtimeFirst) {
      ensure(await b.value(`window.__fontSharingExternalWaiters.length>0`), 'the static consumer is actually delayed before the real runtime starts');
      await releaseWasm();
      ensure(await until(b, `window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&a.state==='ready')`), 'runtime installs the initial font before the delayed static consumer');
      const before = count(initial);
      await b.value(`window.__fontSharingExternalReady=true;window.__fontSharingExternalWaiters.splice(0).forEach(resolve=>resolve());true`);
      ensure(await until(b, `window.omuiStaticFonts.loaded.includes('Inter.ttf')`), 'the delayed static consumer loads a real face from ready runtime bytes');
      ensure(count(initial)===before && before===1, 'runtime-first and later DOM consumption make one initial-font request', {before,after:count(initial)});
    } else if (scenario.pending || scenario.retry) {
      for (let i=0;!fontResponse&&i<100;i++) await sleep(50);
      ensure(!!fontResponse, 'the initial font response is genuinely held before preRun');
      await b.value(`window.__fontSharingLeases={resolved:0,error:null,values:[]};
        Promise.all([window.omuiLoad.fontBytes('Inter.ttf'),window.omuiLoad.fontBytes('Inter.ttf')]).then(values=>{
          window.__fontSharingLeases.values=values;window.__fontSharingLeases.resolved=values.length;
        },error=>{window.__fontSharingLeases.error=String(error.message||error);});true`);
      await sleep(100);
      const pending = await b.value(`({static:window.omuiStaticFonts,leases:window.__fontSharingLeases.resolved,runtimeInitialized:window.omuiLoad.runtimeInitialized})`);
      ensure(pending.static.pending.includes('Inter.ttf') && pending.leases===0 && pending.runtimeInitialized===false, 'DOM and concurrent lease consumers stay pending before preRun', pending);
      await releaseWasm();
      ensure(await until(b, `window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&a.state==='fetching')`), 'runtime preRun joins the same pending font response');
      ensure(count(initial)===1, 'all pending consumers share exactly one CDP request', {requests:count(initial)});
      if (scenario.retry) {
        const failed=fontResponse; fontResponse=null;
        await b.call('Fetch.fulfillRequest',{requestId:failed,responseCode:404,responseHeaders:[{name:'Content-Type',value:'text/plain'}],body:''});
        ensure(await until(b, `window.omuiStaticFonts.error && window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&a.state==='error')`), 'one failed request reaches both consumers as a recoverable error');
        const failure = await staticState();
        ensure(!failure.handed && !failure.hidden && !failure.state.loaded.includes('Inter.ttf'), 'font failure retains readable static content and does not report a ready face',failure.state);
        const bounds=await b.value(`(()=>{let r=document.getElementById('omui-load-retry').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2,w:r.width,h:r.height};})()`);
        ensure(bounds?.w>0&&bounds?.h>0, 'an actual visible retry control is available');
        for(const type of ['mouseMoved','mousePressed','mouseReleased']) await b.call('Input.dispatchMouseEvent',{type,x:bounds.x,y:bounds.y,button:'left',clickCount:1});
        for (let i=0;!fontResponse&&i<100;i++) await sleep(50);
        ensure(!!fontResponse && count(initial)===2, 'simultaneous DOM/runtime retries share one new request', {requests:count(initial)});
        ensure(await b.value(`!window.omuiStaticFonts.loaded.includes('Inter.ttf') && !window.omuiLoad.assets.some(a=>a.id==='initial-font:Inter.ttf'&&a.state==='ready')`), 'a held retry cannot prematurely report either consumer ready');
      }
      await releaseFont();
      if (!scenario.retry) {
        ensure(await until(b, `window.__fontSharingLeases.resolved===2`), 'both pending public leases resolve after the shared response');
        log.concurrent_leases=await b.value(`(async()=>{let values=window.__fontSharingLeases.values;let result=await Promise.all(values.map(async v=>({id:v.id,url:v.url,bytes:v.buffer.byteLength,arrayBuffer:v.buffer instanceof ArrayBuffer,sha256:Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',v.buffer))).map(x=>x.toString(16).padStart(2,'0')).join('')})));values[0].release();values[0].release();result[1].surviving_bytes=values[1].buffer.byteLength;values[1].release();values[1].release();return result;})()`);
        const expected=fontFiles.get(initial);
        ensure(log.concurrent_leases.length===2&&log.concurrent_leases.every(x=>x.arrayBuffer&&x.bytes===expected.length&&x.sha256===expected.sha256&&x.id==='initial-font:Inter.ttf'&&new URL(x.url,server.url).pathname.slice(1)===initial)&&log.concurrent_leases[1].surviving_bytes>0, 'pending leases return the actual initial font; repeated release preserves the other consumer',log.concurrent_leases);
      }
    } else {
      ensure(await until(b, `window.omuiStaticFonts.loaded.includes('Inter.ttf') && window.omuiStaticFonts.pending.length===0`), 'static FontFaces finish while wasm is held');
      ensure(await b.value(`window.omuiLoad.runtimeInitialized===false`), 'static consumers complete before runtime startup');
      await releaseWasm();
    }

    ensure(await until(b, `window.__fontSharingPresented && window.omuiLoad.presented===true && window.omuiStaticFonts.pending.length===0 && window.omuiLoad.assets.filter(a=>a.kind==='font'&&a.group==='startup').every(a=>['ready','skipped'].includes(a.state))`), 'real native presentation and runtime startup fonts become ready');
    log.before_handoff=await staticState();
    const state=log.before_handoff.state;
    ensure(!log.before_handoff.hidden&&!log.before_handoff.handed&&state.active&&state.requested.length>0&&state.pending.length===0&&!state.error,
      'the observation barrier retains a genuinely loaded static surface alongside runtime fonts',state);
    ensure(state.requested.every(name=>state.loaded.includes(name)&&log.before_handoff.faces.some(f=>f.family===families[name]&&f.status==='loaded')),
      'every requested static font is an actual loaded DOM FontFace',log.before_handoff.faces);
    ensure(state.requested.every(name=>log.before_handoff.assets.some(a=>(a.id==='initial-font:'+name||a.id==='font:'+name)&&a.state==='ready')),
      'each static font also has ready runtime installation');
    if(scenario.route==='docs/start')ensure(state.loaded.includes('JetBrainsMono.ttf'),'documentation uses the real code FontFace, not only body text');
    if(scenario.route.startsWith('zh/'))ensure(state.loaded.includes('NotoSansSC.ttf'),'Chinese static content uses the actual Chinese face');
    if(!scenario.pending&&!scenario.runtimeFirst&&!scenario.retry) {
      const capture=await b.call('Page.captureScreenshot',{format:'png'});
      await writeFile('target/site-font-delivery/'+scenario.name+'.png',Buffer.from(capture.result.data,'base64'));
    }
    await b.value(`window.__fontSharingHold=false;window.omuiLoad.fontBytes('Inter.ttf').then(lease=>{lease.release();return true;})`);
    ensure(await until(b, `window.omuiStaticHandedOff && window.omuiStaticFonts.active===false && window.omuiLoad.phase==='complete'`), 'handoff releases static ownership and all planned font loads complete');
    log.after_handoff=await staticState();
    ensure(['requested','loaded','pending'].every(key=>log.after_handoff.state[key].length===0)&&!log.after_handoff.faces.some(f=>Object.values(families).includes(f.family)),
      'handoff removes owned DOM faces and clears static-font data',log.after_handoff.state);
    ensure(log.after_handoff.assets.filter(a=>a.kind==='font'&&a.state==='ready').every(a=>count(new URL(a.url,server.url).pathname.slice(1))===(scenario.retry&&a.url===initial?2:1)),
      'each installed font URL has exactly one real download per expected attempt');
    const beforeLate=log.requests.length;
    log.full_lease=await b.value(`(async()=>{let a=await window.omuiLoad.fontBytes('Inter.ttf');let hash=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',a.buffer))).map(x=>x.toString(16).padStart(2,'0')).join('');let value={id:a.id,url:a.url,bytes:a.buffer.byteLength,arrayBuffer:a.buffer instanceof ArrayBuffer,sha256:hash};a.release();a.release();return value;})()`);
    const full=fontFiles.get(inter[1]);
    ensure(log.full_lease?.id==='font:Inter.ttf'&&new URL(log.full_lease.url,server.url).pathname.slice(1)===inter[1]&&log.full_lease.arrayBuffer&&log.full_lease.bytes===full.length&&log.full_lease.sha256===full.sha256,
      'a late lease identifies and returns the actual full FS font, not initial bytes or an initial URL',log.full_lease);
    await sleep(100);
    ensure(log.requests.length===beforeLate,'late ready-font reads and repeated release perform no network requests');
    ensure(log.exceptions.length===0,'the browser has no unexpected runtime exceptions',log.exceptions);
  } catch(error) { log.error=String(error.message||error); check(false,scenario.name+': scenario completes',log.error); }
  finally {
    if(b) {
      if(wasm)await b.call('Fetch.continueRequest',{requestId:wasm});
      if(fontResponse)await b.call('Fetch.continueResponse',{requestId:fontResponse});
      log.network=[...fontFiles].map(([path,identity])=>{
        const rows=log.requests.filter(r=>r.path===path);
        return {path,name:identity.name,variant:identity.variant,requests:rows.length,
          types:Object.fromEntries([...new Set(rows.map(r=>r.type))].map(type=>[type,rows.filter(r=>r.type===type).length])),
          encoded_body_bytes:rows.reduce((sum,r)=>sum+r.encoded_body_bytes,0),encoded_transport_bytes:rows.reduce((sum,r)=>sum+(r.encoded_transport_bytes||0),0)};
      });
      if(supported) {
        check(log.network.every(f=>f.requests<=(scenario.retry&&f.path===initial?2:1)),scenario.name+': each content URL has one actual request per allowed attempt, with HTTP cache disabled',log.network);
        check(!log.requests.some(r=>r.cache||r.service_worker),scenario.name+': HTTP or service-worker caches do not hide repeated consumers');
        if(scenario.route.startsWith('zh/'))check(count(fonts.find(f=>f[0]==='NotoSansSC.ttf')[3])===0,scenario.name+': direct Chinese startup skips the initial Chinese subset');
      }
      b.close();
    }
    await rm(profile,{recursive:true,force:true,maxRetries:3,retryDelay:100});
  }
}
server.close();
report.ok=report.checks.every(c=>c.ok);
console.log(JSON.stringify(report,null,2));
process.exit(report.ok?0:1);
