// Real browser input and intermediate rendered frames of the shared Inspector workspace.
import { mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { browser, serve, sleep } from './page.mjs';

const output = 'target/site-reactions';
const server = await serve(process.argv[2] || 'target/site');
const b = await browser(process.env.CHROME, { width: 1280, height: 1000 });
const report = { browser: b.version, checks: [], samples: [],
  scope: 'Actual browser pixels and input; offline process samples do not certify real-time performance.' };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail ? { detail } : {}) });
const root = 'main/body/view/reactions/page/cellular', viewport = 'main/body/view/reactions';
const node = id => 'document.querySelector(\'[data-omui-id="' + id + '"]\')';
const until = async expr => {
  for (let i = 0; i < 300; ++i) { if (await b.value(expr)) return true; await sleep(100); }
  return false;
};
const rendered = () => b.value('new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(()=>resolve(true))))');
const invoke = async (op, args = {}) => {
  const r = await b.value('window.omui.invoke(' + JSON.stringify('cellular.' + op) + ',' + JSON.stringify(args) + ')');
  if (!r || r.exitCode !== 0) throw new Error(op + ': ' + JSON.stringify(r));
  await rendered(); return r.data;
};
const bounds = id => b.value('window.omui.snapshot({root:' + JSON.stringify(id) + ',depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)');
const intoView = async id => {
  for (let i = 0; i < 20; ++i) {
    const box = await bounds(id), view = await bounds(viewport);
    if (!Array.isArray(box) || !Array.isArray(view)) { await sleep(100); continue; }
    if (!box.every(Number.isFinite)) throw new Error('Invalid bounds: ' + id);
    if (box[1] >= view[1] && box[1] + box[3] <= view[1] + view[3]) return box;
    await b.call('Input.dispatchMouseEvent', { type: 'mouseWheel', x: view[0] + view[2] / 2,
      y: view[1] + view[3] / 2, deltaX: 0, deltaY: box[1] + box[3] / 2 - view[1] - view[3] / 2 });
    await sleep(200);
  }
  throw new Error('Control did not enter viewport: ' + id);
};
const point = async (x, y) => {
  for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased']) {
    const r = await b.call('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 });
    if (r.error) throw new Error(r.error.message);
  }
  await rendered();
};
const click = async id => { const [x, y, w, h] = await intoView(id); await point(x + w / 2, y + h / 2); };
const shot = async (name, box) => {
  await rendered();
  const r = await b.call('Page.captureScreenshot', { format: 'png', ...(box ? {
    clip: { x: box[0], y: box[1], width: box[2], height: box[3], scale: 1 } } : {}) });
  if (r.error || !r.result?.data) throw new Error('Screenshot failed: ' + JSON.stringify(r.error));
  const bytes = Buffer.from(r.result.data, 'base64'), path = output + '/' + name + '.png';
  await writeFile(path, bytes);
  return { image: path, sha256: createHash('sha256').update(bytes).digest('hex') };
};
try {
  await mkdir(output, { recursive: true });
  await b.call('Page.navigate', { url: server.url + '#/explore/reactions' });
  const ready = await until('window.omuiStaticHandedOff && ' + node(root + '/field'));
  check(ready, 'the website opens the shared reaction workspace');
  if (!ready) throw new Error('Workspace did not become ready');
  check(!await b.value(node(root + '/details')), 'rules and project details start closed');
  report.initial = await invoke('project');
  report.opening = await shot('opening-en');

  const box = await intoView(root + '/field'), cell = Math.min(box[2] / 64, box[3] / 36);
  await point(box[0] + (box[2] - cell * 64) / 2 + 49.5 * cell,
    box[1] + (box[3] - cell * 36) / 2 + 27.5 * cell);
  const human = await invoke('snapshot');
  check((await invoke('inspect', { x: 49, y: 27 })).values[1] === 1, 'a real pointer paints the intended cell');
  await invoke('reset');
  await invoke('paint', { x: 49, y: 27, radius: 1, brush: 'spark' });
  check((await invoke('snapshot')).state_hash === human.state_hash, 'Agent and pointer produce identical material state');

  await invoke('reset');
  await b.call('Input.dispatchMouseEvent', { type: 'mouseMoved', x: 1, y: 1 });
  const canvas = await intoView(root + '/field');
  for (let i = 0; i <= 24; ++i) {
    if (i) await invoke('advance', { count: 1 });
    const state = await invoke('snapshot');
    report.samples.push({ frame: state.frame, tick: state.tick, state_hash: state.state_hash,
      time_numerator: i, time_denominator: state.rate, bounds: canvas,
      ...await shot('tick-' + String(i).padStart(2, '0'), canvas) });
  }
  check(new Set(report.samples.map(s => s.state_hash)).size === 25, 'all 25 intermediate states differ');
  check(new Set(report.samples.map(s => s.sha256)).size === 25, 'all 25 actual canvas images differ');
  for (const frame of [17, 8, 0]) {
    const state = await invoke('seek', { frame }), image = await shot('reverse-' + frame, canvas);
    check(state.state_hash === report.samples[frame].state_hash && image.sha256 === report.samples[frame].sha256,
      'reverse seek reproduces state and actual canvas pixels at frame ' + frame);
  }

  await click(root + '/more');
  await click(root + '/details/config');
  if (!await until('document.activeElement?.matches(\'textarea[data-omui="input"]\')')) throw new Error('Editor did not focus');
  for (const type of ['keyDown', 'keyUp'])
    await b.call('Input.dispatchKeyEvent', { type, key: 'a', code: 'KeyA', windowsVirtualKeyCode: 65, modifiers: 2 });
  await b.call('Input.insertText', { text: '{"rate":18}' });
  await click(root + '/details/actions/apply');
  check((await invoke('snapshot')).rate === 18, 'real keyboard input edits and applies the rule configuration');

  await click(root + '/transport/play');
  await sleep(500);
  check((await invoke('snapshot')).tick > 0, 'real Play input advances the live simulation');
  const brand = await bounds('main/bar/mark');
  await point(brand[0] + brand[2] / 2, brand[1] + brand[3] / 2);
  if (!await until('!' + node(root + '/field'))) throw new Error('Navigation did not leave reaction workspace');
  const paused = await invoke('snapshot');await sleep(300);
  check(!paused.playing && (await invoke('snapshot')).tick === paused.tick, 'leaving the route cancels ongoing playback');

  await b.call('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  await b.value('location.hash="#/zh/explore/reactions"');
  if (!await until(node(root + '/field'))) throw new Error('Chinese route did not open');
  check((await invoke('snapshot')).state_hash === paused.state_hash, 'navigation and language changes preserve the edited project');
  await intoView(root + '/field');
  const phone = await bounds(root + '/field');
  check(phone[0] >= 0 && phone[2] > 0 && phone[0] + phone[2] <= 390.5, 'the real resized phone canvas fits its viewport');
  report.phone = await shot('phone-zh');
} catch (error) { check(false, 'browser reaction scenario completed', String(error.stack || error)); }
finally { await b.close(); server.close(); }
report.ok = report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2));process.exit(report.ok ? 0 : 1);
