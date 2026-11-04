// Live CPU canvas evidence, independent of semantic target coordinates.
// Mutations use trusted CDP pointer/keyboard/resize input. The public bridge
// only reads snapshots and history. This is correctness coverage, not a perf test.
import { mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';

const server = await serve(process.argv[2] || 'target/site');
const output = process.argv[3] || 'target/site-transition';
await mkdir(output, { recursive: true });
const report = { checks: [], scenarios: [], exceptions: [], renderer: 'cpu',
  pixelSelection: 'Bottommost connected saturated site-accent fill below y=200; at least 65×12 CSS px and 400 pixels; disabled fills and disconnected text excluded.',
  coverage: { earlyWindowMs: 250, maxPixelAgeMs: 80, note: 'An early-motion sampling window; not a product latency budget.' } };
const root = 'main/body/view/agent/page/agent_feature', q = JSON.stringify;
let b;
const check = (ok, what, detail, kind = 'interaction') => report.checks.push({ ok: !!ok, what, kind, ...(detail === undefined ? {} : { detail }) });
const dispatch = async (method, params) => { const r = await b.call(method, params); if (r.error) throw new Error(r.error.message); return r; };
const nodes = id => b.value(`window.omui.snapshot({root:${q(id)},depth:0}).then(r=>r.exitCode===0?r.data?.nodes:[])`);
const node = async id => (await nodes(id))?.find(n => n.id === id);
const live = n => !!n && !n.state?.includes('removing') && !n.state?.includes('hidden');
const cards = async () => live(await node(root + '/specimen/decision_card'));
async function until(predicate, timeout = 15000) {
  const end = Date.now() + timeout;
  while (Date.now() < end) { if (await predicate()) return true; await sleep(40); }
  return false;
}
async function click(point) {
  await dispatch('Input.dispatchMouseEvent', { type: 'mouseMoved', ...point });
  for (const type of ['mousePressed', 'mouseReleased'])
    await dispatch('Input.dispatchMouseEvent', { type, ...point, button: 'left', clickCount: 1 });
}
async function key(name, code, virtual) {
  for (const type of ['keyDown', 'keyUp'])
    await dispatch('Input.dispatchKeyEvent', { type, key: name, code, windowsVirtualKeyCode: virtual });
}
// Setup controls are stationary, semantic positions are suitable for them.
// The moving primary below is always selected from painted pixels instead.
async function setupClick(id) {
  for (let i = 0; i < 20; ++i) {
    const n = await node(id), view = await node('main/body/view/agent');
    if (!live(n) || !n.bounds || !view?.bounds) { await sleep(40); continue; }
    const [x, y, w, h] = n.bounds, [vx, vy, vw, vh] = view.bounds;
    if (w > 0 && h > 0 && y >= vy + 1 && y + h <= vy + vh - 1) { await click({ x: x + w / 2, y: y + h / 2 }); return; }
    await dispatch('Input.dispatchMouseEvent', { type: 'mouseWheel', x: vx + vw * 0.85, y: vy + vh / 2,
      deltaX: 0, deltaY: y + h / 2 - (vy + vh / 2) });
    await sleep(80);
  }
  throw new Error('Stationary setup control is not reachable: ' + id);
}

// Executed in Chromium, using only DOM canvas APIs. Accent detection ignores
// the header, tiny glyph components, and the muted disabled Record button.
// Returned points are actual colored pixels, never semantic-box centers.
function recordCanvas(duration) {
  const run = { start: performance.now(), frames: [], events: [], done: false, error: null };
  window.__omuiTransitionCapture = run;
  const events = ['pointerdown', 'pointerup', 'keydown', 'keyup', 'resize'];
  const observe = event => run.events.push({ type: event.type, at: performance.now(), trusted: event.isTrusted,
    x: event.clientX, y: event.clientY, key: event.key, width: innerWidth, height: innerHeight });
  events.forEach(type => addEventListener(type, observe, true));
  function sample() {
    try {
      const canvas = document.getElementById('canvas'), context = canvas?.getContext('2d');
      if (!context) throw new Error('The official ?renderer=cpu canvas is not readable');
      const box = canvas.getBoundingClientRect(), w = canvas.width, h = canvas.height;
      const rgba = context.getImageData(0, 0, w, h).data, mask = new Uint8Array(w * h), queue = new Int32Array(w * h);
      const sx = w / box.width, sy = h / box.height, minY = Math.max(0, Math.floor((200 - box.y) * sy));
      const maxY = Math.min(h, Math.ceil((innerHeight - 44 - box.y) * sy));
      for (let y = minY; y < maxY; ++y) for (let x = 0; x < w; ++x) {
        const i = y * w + x, p = i * 4, r = rgba[p], g = rgba[p + 1], blue = rgba[p + 2];
        if (r < 155 && g < 155 && blue > 210 && blue > r + 100 && blue > g + 100 && rgba[p + 3] > 230) mask[i] = 1;
      }
      const components = [];
      for (let y = minY; y < maxY; ++y) for (let x = 0; x < w; ++x) {
        const first = y * w + x; if (!mask[first]) continue;
        let head = 0, tail = 1, left = x, right = x, top = y, bottom = y;
        queue[0] = first; mask[first] = 0;
        while (head < tail) {
          const i = queue[head++], xx = i % w, yy = Math.floor(i / w);
          left = Math.min(left, xx); right = Math.max(right, xx); top = Math.min(top, yy); bottom = Math.max(bottom, yy);
          for (const next of [xx > 0 ? i - 1 : -1, xx + 1 < w ? i + 1 : -1, yy > minY ? i - w : -1, yy + 1 < maxY ? i + w : -1])
            if (next >= 0 && mask[next]) { mask[next] = 0; queue[tail++] = next; }
        }
        const bounds = [box.x + left / sx, box.y + top / sy, (right - left + 1) / sx, (bottom - top + 1) / sy];
        if (bounds[2] < 65 || bounds[3] < 12 || tail / (sx * sy) < 400) continue;
        const cx = (left + right) / 2, cy = (top + bottom) / 2;
        let selected = first, nearest = Infinity;
        for (let i = 0; i < tail; ++i) {
          const px = queue[i] % w, py = Math.floor(queue[i] / w), distance = (px - cx) ** 2 + (py - cy) ** 2;
          if (distance < nearest) { selected = queue[i]; nearest = distance; }
        }
        components.push({ bounds, area: tail / (sx * sy), point: { x: box.x + (selected % w + 0.5) / sx, y: box.y + (Math.floor(selected / w) + 0.5) / sy },
          rgba: Array.from(rgba.slice(selected * 4, selected * 4 + 4)) });
      }
      // The disabled Record is above playback and the task primary. Select
      // the bottommost substantial painted button, even if a disabled fill
      // happens to share its hue; never consult semantic coordinates here.
      components.sort((a, c) => (c.bounds[1] + c.bounds[3]) - (a.bounds[1] + a.bounds[3]) || c.area - a.area);
      run.frames.push({ at: performance.now(), viewport: [innerWidth, innerHeight], canvas: [w, h], canvasBox: [box.x, box.y, box.width, box.height],
        components, png: canvas.toDataURL('image/png') });
    } catch (error) { run.error = String(error.stack || error); }
    if (!run.error && performance.now() - run.start < duration) requestAnimationFrame(sample);
    else { run.done = true; events.forEach(type => removeEventListener(type, observe, true)); }
  }
  requestAnimationFrame(sample);
  return run.start;
}

async function history() {
  return b.value(`window.omui.request({op:'history',scope:${q(root + '/preview/actions/arrange')}}).then(r=>({exitCode:r.exitCode,data:r.data}))`);
}
async function savePhase(scenario, phase, record) {
  const done = await until(() => b.value('window.__omuiTransitionCapture?.done'), 5000);
  check(done, `${scenario.name}/${phase.name}: continuous capture completes`, undefined, 'coverage');
  const run = await b.value('window.__omuiTransitionCapture');
  if (!run) throw new Error('No canvas recording');
  check(!run.error, `${scenario.name}/${phase.name}: all captured frames come from the real CPU canvas`, run.error, 'coverage');
  const directory = output + '/' + scenario.name + '/' + phase.name;
  await mkdir(directory, { recursive: true });
  for (let i = 0; i < run.frames.length; ++i) {
    const frame = run.frames[i], file = directory + `/frame-${String(i).padStart(3, '0')}.png`;
    await writeFile(file, Buffer.from(frame.png.split(',')[1], 'base64'));
    delete frame.png; frame.file = file;
  }
  record.capture = run;
  const screenshot = await dispatch('Page.captureScreenshot', { format: 'png' });
  const file = directory + '/browser.png';
  await writeFile(file, Buffer.from(screenshot.result.data, 'base64')); record.screenshot = file;
  record.trace = directory + '/frames.motion.omui.json';
}

async function phase(scenario, spec) {
  const record = { name: spec.name, beforeCards: await cards(), beforeHistory: await history() };
  scenario.phases.push(record);
  await b.value(`(${recordCanvas.toString()})(650)`);
  try {
    await spec.trigger();
    let probe;
    for (let i = 0; i < 20; ++i) {
      probe = await b.value(`(async()=>{const c=window.__omuiTransitionCapture;const r=await window.omui.snapshot({root:${q(root + '/preview/actions/arrange')},depth:0});const f=c?.frames.at(-1);return {now:performance.now(),start:c?.start,frame:f?{at:f.at,components:f.components}:null,node:r.data?.nodes?.find(n=>n.id===${q(root + '/preview/actions/arrange')})}})()`);
      if (live(probe?.node) && probe.frame?.components?.length) break;
      if (probe && probe.now - probe.start > report.coverage.earlyWindowMs) break;
      await sleep(8);
    }
    record.probe = probe;
    const painted = probe?.frame?.components?.[0];
    const early = painted && probe.now - probe.start <= report.coverage.earlyWindowMs && probe.now - probe.frame.at < report.coverage.maxPixelAgeMs;
    check(early, `${scenario.name}/${spec.name}: a fresh painted primary is covered in the early motion window`, probe, 'coverage');
    if (!early) throw Object.assign(new Error('Coverage gap: sampling/CDP missed the early window; this does not establish a product timing failure'), { name: 'CoverageGap' });
    check(probe.node.state?.includes('animating'), `${scenario.name}/${spec.name}: the real primary is still moving at the pixel probe`, probe.node.state, 'coverage');
    // Record semantic bounds as a comparison, never as the source of this point.
    record.pixelPoint = painted.point;
    await click(painted.point);
    record.dispatchedAt = await b.value('performance.now()');
    record.deliveredInput = await b.value(`(()=>{const p=${q(painted.point)};return [...window.__omuiTransitionCapture.events].reverse().find(e=>e.type==='pointerup'&&Math.abs(e.x-p.x)<=1&&Math.abs(e.y-p.y)<=1)})()`);
    const received = record.deliveredInput;
    record.coveredInput = !!received?.trusted && received.at - probe.start <= report.coverage.earlyWindowMs && received.at - probe.frame.at <= report.coverage.maxPixelAgeMs;
    if (!record.coveredInput) {
      record.afterCards = await cards(); record.afterHistory = await history();
      throw Object.assign(new Error('Coverage gap: trusted input arrived after the sampled pixels became stale or after the early window'), { name: 'CoverageGap' });
    }
    const changed = await until(async () => await cards() !== record.beforeCards, 3000);
    record.afterCards = await cards(); record.afterHistory = await history();
    record.afterPause = await node(root + '/preview/actions/pause'); record.afterOwnership = await node(root + '/preview/state');
    check(record.afterPause?.state?.includes('disabled') && /Yours to use|现在由你操作/.test(record.afterOwnership?.name || ''),
      `${scenario.name}/${spec.name}: the pointer took ownership instead of starting a Replay simulation`, { pause: record.afterPause, ownership: record.afterOwnership });
    check(changed, `${scenario.name}/${spec.name}: clicking actual painted pixels changes the real task grouping`, record.afterCards);
    const undo = record.afterHistory.data?.undo || [];
    check(record.afterHistory.exitCode === 0 && undo.length > (record.beforeHistory.data?.undo?.length || 0) && undo.at(-1)?.label?.includes(root + '/preview/actions/arrange'),
      `${scenario.name}/${spec.name}: public history records the new Arrange action`, record.afterHistory);
  } finally {
    await savePhase(scenario, spec, record);
    const events = record.capture?.events || [];
    const up = [...events].reverse().find(e => e.type === 'pointerup' && record.pixelPoint && Math.abs(e.x - record.pixelPoint.x) <= 1 && Math.abs(e.y - record.pixelPoint.y) <= 1);
    record.receivedInput = up;
    check(!!up?.trusted, `${scenario.name}/${spec.name}: Chromium received a trusted pointer at the sampled ink`, up, 'coverage');
    check(up && record.probe?.frame && up.at - record.probe.frame.at <= report.coverage.maxPixelAgeMs,
      `${scenario.name}/${spec.name}: input refers to freshly sampled painted pixels`, up && record.probe?.frame && up.at - record.probe.frame.at, 'coverage');
    check(up && up.at - record.capture.start <= report.coverage.earlyWindowMs,
      `${scenario.name}/${spec.name}: trusted input actually arrived in the early coverage window`, up && up.at - record.capture.start, 'coverage');
    check((record.capture?.frames.length || 0) >= 8, `${scenario.name}/${spec.name}: continuous frames remain available for review`, record.capture?.frames.length, 'coverage');
    await writeFile(record.trace, JSON.stringify(record, null, 2) + '\n');
  }
}

try {
  for (const width of [390, 1280]) for (const language of ['en', 'zh']) {
    const scenario = { name: `${language}-${width}`, width, language, phases: [] }; report.scenarios.push(scenario);
    b = await browser(process.env.CHROME, { width, height: 844 });
    b.on((method, params) => { if (method === 'Runtime.exceptionThrown') report.exceptions.push({ scenario: scenario.name, detail: params.exceptionDetails }); });
    try {
      await dispatch('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
      await dispatch('Page.navigate', { url: server.url + '?renderer=cpu#/' + (language === 'zh' ? 'zh/' : '') + 'agent/develop' });
      const ready = await until(() => b.value(`window.omuiStaticHandedOff && !!window.omui && !!document.querySelector('[data-omui-id=${q(root + '/preview/actions/verify')}]')`));
      check(ready, `${scenario.name}: the final candidate exposes the real Verify primary`); if (!ready) throw new Error('Live Agent page did not open');
      await setupClick(root + '/preview/actions/pause');
      const paused = await until(async () => (await node(root + '/preview/actions/pause'))?.state?.includes('disabled'));
      check(paused, `${scenario.name}: a real Pause click stops the scripted preview`);
      if (!paused) throw new Error('Could not pause the scripted preview through real input');
      check((await node(root + '/specimen/record'))?.state?.includes('disabled'), `${scenario.name}: the draft Record stays disabled above the playback/task region`);
      await phase(scenario, { name: 'mode-switch', trigger: () => setupClick(root + '/modes/design') });
      await setupClick(root + '/modes/design');
      await key('ArrowLeft', 'ArrowLeft', 37);
      check(await until(async () => (await node(root + '/modes'))?.value === 'develop'), `${scenario.name}: real keyboard input selects Develop`);
      await phase(scenario, { name: 'mode-and-narrow', trigger: async () => {
        await key('ArrowRight', 'ArrowRight', 39);
        await sleep(30);
        await dispatch('Emulation.setDeviceMetricsOverride', { width: 360, height: 844, deviceScaleFactor: 1, mobile: false });
      } });
      await phase(scenario, { name: 'restore-width', trigger: () => dispatch('Emulation.setDeviceMetricsOverride', { width, height: 844, deviceScaleFactor: 1, mobile: false }) });
    } catch (error) { check(false, `${scenario.name}: transition scenario completes`, String(error.stack || error), error.name === 'CoverageGap' ? 'coverage' : 'interaction'); }
    finally { b.close(); b = null; }
  }
  check(report.exceptions.length === 0, 'no unexpected browser runtime exceptions', report.exceptions);
} finally { if (b) b.close(); server.close(); }
report.ok = report.checks.every(c => c.ok);
await writeFile(output + '/report.motion.omui.json', JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report, null, 2)); process.exitCode = report.ok ? 0 : 1;
