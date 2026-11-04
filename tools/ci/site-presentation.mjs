// First-view density and real input after preview handoff. Semantic calls only
// read mounted controls; changes go through Chromium pointer/keyboard input.
import { mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';

const server = await serve(process.argv[2] || 'target/site');
const output = process.argv[3] || 'target/site-presentation';
const report = { checks: [], screenshots: [], exceptions: [] };
await mkdir(output, { recursive: true });
const check = (ok, what, detail) => {
  report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : { detail }) });
};
const q = JSON.stringify;
let b, place, width, height;
const wait = async predicate => {
  for (let i = 0; i < 200; ++i) { if (await predicate()) return true; await sleep(75); }
  return false;
};
const nodes = root => b.value(`window.omui.snapshot({root:${q(root)},depth:-1}).then(r=>r.exitCode===0?r.data?.nodes:[])`);
const node = async id => (await nodes(id))?.find(n => n.id === id);
const scalar = async id => { const v = (await node(id))?.value; return v && typeof v === 'object' && 'value' in v ? v.value : v; };
const checked = async id => !!(await node(id))?.state?.includes('checked');
const present = async id => !!await b.value(`document.querySelector('[data-omui-id="'+${q(id)}+'"]')`);
async function requireState(predicate, what) {
  const ok = await wait(predicate); check(ok, what);
  if (!ok) throw new Error(what);
}
async function pointer(id, fraction = 0.5, moveOnly = false) {
  for (let i = 0; i < 20; ++i) {
    const n = await node(id), view = id.startsWith('main/bar/') ? [0, 0, width, height] : (await node('main/body/view/' + place))?.bounds;
    if (!n?.bounds || !view || n.state?.includes('hidden')) { await sleep(80); continue; }
    const [x, y, w, h] = n.bounds, [vx, vy, vw, vh] = view;
    if (y >= vy + 1 && y + h <= vy + vh - 1 && x + w * fraction >= vx && x + w * fraction <= vx + vw) {
      const point = { x: x + w * fraction, y: y + h / 2 };
      await b.call('Input.dispatchMouseEvent', { type: 'mouseMoved', ...point });
      if (!moveOnly) for (const type of ['mousePressed', 'mouseReleased'])
        await b.call('Input.dispatchMouseEvent', { type, ...point, button: 'left', clickCount: 1 });
      await sleep(80); return;
    }
    await b.call('Input.dispatchMouseEvent', { type: 'mouseWheel', x: vx + vw * 0.8, y: vy + vh / 2,
      deltaX: 0, deltaY: y + h / 2 - (vy + vh / 2) });
    await sleep(120);
  }
  throw new Error('Control did not enter the visible stage: ' + id);
}
async function key(name, code, virtual, modifiers = 0) {
  for (const type of ['keyDown', 'keyUp']) await b.call('Input.dispatchKeyEvent', {
    type, key: name, code, windowsVirtualKeyCode: virtual, modifiers
  });
}
async function type(id, text) {
  await pointer(id);
  await requireState(() => b.value(`document.activeElement?.matches('textarea[data-omui="input"]')`), 'the real editor has browser focus');
  await key('a', 'KeyA', 65, 2);
  await b.call('Input.insertText', { text });
  await requireState(async () => await scalar(id) === text, 'trusted input reaches the current editor');
}
async function open(route, page) {
  place = page;
  await b.call('Page.navigate', { url: server.url + '#/' + route });
  await requireState(() => b.value(`window.omuiStaticHandedOff && !!window.omui && !!document.querySelector('[data-omui-id="main/body/view/${page}/page"]')`), 'live page ready: ' + route);
}
async function screenshot(name) {
  // Reference stills describe the settled state. Continuous transitions have
  // separate native recordings; a short text/layout morph is not a still.
  await sleep(500);
  const result = await b.call('Page.captureScreenshot', { format: 'png' });
  if (!result.result?.data) throw new Error('Screenshot failed: ' + name);
  const file = output + '/' + name + '.png';
  await writeFile(file, Buffer.from(result.result.data, 'base64')); report.screenshots.push(file);
}

try {
  for (const [route, expected] of [['motion', 'shape-morph'], ['zh/demos', 'palette-studio']]) {
    width = 390; height = 844;
    b = await browser(process.env.CHROME, { width, height });
    try {
      await b.call('Emulation.setScriptExecutionDisabled', { value: true });
      await b.call('Page.navigate', { url: server.url + '#/' + route });
      await requireState(() => b.value(`Array.from(document.querySelectorAll('[data-gallery-current=${q(expected)}]')).some(n=>n.getBoundingClientRect().width>0)`), 'NoScript has one readable selected specimen: ' + route);
      const visible = await b.value(`Array.from(document.querySelectorAll('[data-gallery-catalog]')).filter(n=>n.getBoundingClientRect().width>0).map(n=>({open:n.open,links:n.querySelectorAll('a').length,summary:n.querySelector('summary').textContent}))`);
      check(visible?.length === 1 && !visible[0].open && visible[0].links >= (route.includes('demos') ? 20 : 40), 'NoScript catalogue starts folded with every design link retained', visible);
      const clickVisible = async selector => {
        const at = await b.value(`(() => {const n=Array.from(document.querySelectorAll(${q(selector)})).find(n=>n.getBoundingClientRect().width>0);if(!n)return null;n.scrollIntoView({block:'center'});const r=n.getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
        if (!at) throw new Error('No visible static control: ' + selector);
        for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased']) await b.call('Input.dispatchMouseEvent', { type, ...at, button: 'left', clickCount: 1 });
        await sleep(100);
      };
      await clickVisible('[data-gallery-catalog]>summary');
      await clickVisible('[data-gallery-catalog] [data-gallery-group]>summary');
      await clickVisible('[data-gallery-catalog] a');
      check(await b.value(`location.hash.startsWith(${q('#/' + route + '/')})`), 'native catalogue links open the requested detail without JavaScript');
      check(await b.value(`(() => {const current=Array.from(document.querySelectorAll('[data-gallery-current]')).filter(n=>n.getBoundingClientRect().width>0);return current.length===1 && current[0].dataset.galleryCurrent===location.hash.split('/').pop();})()`), 'NoScript navigation displays the selected design rather than the overview default');
      check(await b.value(`(() => {const details=Array.from(document.querySelectorAll('[data-gallery-details]')).filter(n=>n.getBoundingClientRect().width>0);return details.length===1 && !details[0].open;})()`), 'NoScript detail metadata remains progressively disclosed');
    } catch (error) { check(false, 'NoScript presentation ' + route, String(error.stack || error)); }
    finally { b.close(); b = null; }
  }
  for (const [w, lang] of [[360, 'en'], [390, 'en'], [390, 'zh'], [1280, 'en'], [1280, 'zh']]) {
    width = w; height = w < 900 ? 844 : 800;
    b = await browser(process.env.CHROME, { width, height });
    b.on((method, params) => { if (method === 'Runtime.exceptionThrown') report.exceptions.push({ width, lang, error: params.exceptionDetails }); });
    try {
      const prefix = lang === 'zh' ? 'zh/' : '';
      await open(prefix, 'home');
      const root = 'main/body/view/home/page/landing';
      const action = await node(root + '/intro/actions/explore'), visual = await node(root + '/scene/visual');
      check(!!action?.bounds && action.bounds[1] >= 60 && action.bounds[1] + action.bounds[3] <= height - 44,
        `${lang} ${w}: primary destination is visible in the initial viewport`, action?.bounds);
      if (w < 900) check(action?.bounds?.[1] < visual?.bounds?.[1], `${lang} ${w}: destination precedes the full mobile scene`);
      await sleep(500); // Keep geometry checks at first view; capture the brief assembly after it settles.
      await screenshot(`home-${lang}-${w}`);
      await pointer(root + '/intro/actions/explore'); place = 'components';
      await requireState(() => present('main/body/view/components/page'), 'the visible home CTA opens the real component page');

      await open(prefix + 'styles', 'styles');
      const style = 'main/body/view/styles/page/styles_feature';
      check(!await present(style + '/details/radius') && !await present(style + '/previews'), `${lang} ${w}: extra specimens and parameters start unmounted`);
      await screenshot(`styles-initial-${lang}-${w}`);
      if (w < 900) {
        check(await scalar(style + '/comparison/view') === 'styled', `${lang} ${w}: phone starts with the styled specimen`);
        check(!await present(style + '/comparison/default') && await present(style + '/comparison/styled'), `${lang} ${w}: exactly one full-width comparison mounts`);
        const stage = await node(style + '/comparison/styled'), container = await node(style);
        check(!!stage?.bounds && !!container?.bounds && Math.abs(stage.bounds[2] - container.bounds[2]) < 1,
          `${lang} ${w}: the selected specimen uses the full available width`, { stage: stage?.bounds, container: container?.bounds });
        const quiet = style + '/comparison/styled/controls/quiet';
        await pointer(quiet, 0.5, true);
        const before = await checked(quiet);
        await pointer(quiet);
        await requireState(async () => await checked(quiet) !== before, 'the visible styled control changes the shared model');
        await pointer(style + '/comparison/view', 0.25);
        await requireState(() => present(style + '/comparison/default/controls/quiet'), 'a real comparison selection mounts the default view');
        check(await checked(style + '/comparison/default/controls/quiet') !== before, 'switching material view preserves the edited model');
        check(!await present(style + '/comparison/styled'), 'the other comparison is unmounted on phone');
        await screenshot(`styles-default-${lang}-${w}`);
        await pointer(style + '/comparison/view', 0.75);
        await requireState(() => present(quiet), 'the styled view can be revisited');
      } else {
        check(await present(style + '/comparison/default') && await present(style + '/comparison/styled'), `${lang} ${w}: desktop retains the real side-by-side comparison`);
      }
      await screenshot(`styles-${lang}-${w}`);

      await pointer(style + '/actions/details');
      await requireState(() => present(style + '/details/pixel_amount'), 'local effect controls appear only after Details');
      await pointer(style + '/details/pixel_amount', 0.8);
      await requireState(async () => Number(await scalar(style + '/details/pixel_amount')) > 65, 'real pointer changes the chart pixelation strength');
      await pointer(style + '/details/pixel_size', 0.6);
      await pointer(style + '/details/pixel_device');
      await requireState(() => checked(style + '/details/pixel_device'), 'device-pixel units can be selected by a person');
      const copiedEffect = (await node(style + '/details/pixel_source'))?.name || '';
      check(copiedEffect.includes('"device_pixels":1') && copiedEffect.includes('"amount":'), 'displayed configuration follows the actual local effect');
      const amountLabel = await node(style + '/details/pixel_amount_label'), sizeLabel = await node(style + '/details/pixel_size_label');
      check(amountLabel?.name?.includes('%') && sizeLabel?.name?.includes('px') && amountLabel?.bounds?.[3] > 0 && sizeLabel?.bounds?.[3] > 0,
        'people see the parameter names and current values as rendered text');
      await screenshot(`styles-pixel-controls-${lang}-${w}`);
      // Capturing is a native semantic request, not a DOM reconstruction.
      await pointer(style + '/comparison/styled/controls/quiet', 0.5, true);
      const capture = await b.value(`window.omui.request({op:'capture',node:${q(style + '/comparison/styled')},region:[0,0,120,100]})`);
      check(capture?.exitCode === 0 && capture?.data?.base64?.startsWith('iVBORw0KGgo'), 'Agent obtains actual region PNG pixels from the Web renderer');
      await screenshot(`styles-local-pixel-${lang}-${w}`);

      await open(prefix + 'agent/use', 'agent');
      const agent = 'main/body/view/agent/page/agent_feature', title = agent + '/specimen/title';
      await type(title, lang === 'zh' ? '由我接管的想法' : 'My own handoff draft');
      await sleep(2100);
      check(await scalar(title) === (lang === 'zh' ? '由我接管的想法' : 'My own handoff draft') && !await checked(agent + '/specimen/reviewed'),
        `${lang} ${w}: entering and typing cancels pending automatic task steps`);
      await pointer(agent + '/modes', 0.5);
      await requireState(async () => await scalar(agent + '/modes') === 'develop', 'the Develop path opens through real input');
      check(await present(agent + '/preview/actions/verify') && !await present(agent + '/preview/actions/run'), 'Develop uses its own stable Verify action identity');
      check(!await present(agent + '/develop/expected') && !await present(agent + '/develop/requests'), 'development protocol parameters are absent from the overview');
      await pointer(agent + '/method/toggle');
      await requireState(() => present(agent + '/develop/expected'), 'explicit details mounts development controls');
      await pointer(agent + '/modes', 5 / 6);
      await requireState(async () => await scalar(agent + '/modes') === 'design', 'the Design path opens through real input');
      check(await present(agent + '/preview/actions/arrange') && !await present(agent + '/preview/actions/run'), 'Design uses its own stable Arrange action identity');
      check(!await present(agent + '/design/style') && !await present(agent + '/design/spacing'), 'design parameters are absent from the overview');
      await screenshot(`agent-design-${lang}-${w}`);
      await pointer(agent + '/method/toggle');
      await requireState(() => present(agent + '/design/spacing'), 'explicit details mounts design parameters');
    } catch (error) {
      check(false, `presentation scenario ${lang} ${w} completed`, String(error.stack || error));
      try { await screenshot(`failure-${lang}-${w}`); } catch { /* Keep original failure. */ }
    } finally { b.close(); b = null; }
  }
  width = 390; height = 844;
  b = await browser(process.env.CHROME, { width, height });
  b.on((method, params) => { if (method === 'Runtime.exceptionThrown') report.exceptions.push({ width, reduced: true, error: params.exceptionDetails }); });
  try {
    await b.call('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
    await open('agent/use', 'agent');
    const agent = 'main/body/view/agent/page/agent_feature';
    await sleep(600);
    check(!await checked(agent + '/specimen/reviewed'), 'system reduced motion does not start an automatic Agent task');
    await pointer(agent + '/use/actions/play');
    await requireState(async () => (await node(agent + '/specimen/receipt'))?.name?.includes('Recorded here'), 'explicit Play under reduced motion completes real review and record actions');
    check((await nodes(agent + '/use/log'))?.length > 2, 'reduced-motion task completion has real actor records');
    await screenshot('agent-use-reduced-en-390');
  } catch (error) { check(false, 'reduced-motion manual task completed', String(error.stack || error)); }
  finally { b.close(); b = null; }
  check(report.exceptions.length === 0, 'no browser runtime exceptions', report.exceptions);
} finally { if (b) b.close(); server.close(); }
report.ok = report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2));
process.exitCode = report.ok ? 0 : 1;
