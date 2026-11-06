// Real browser input through the site's core, creative and agent paths.
// Gallery changes use real mouse/keyboard input. The deployment-policy case
// also sends native Agent input through the same delivered Web host.
import { mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';

const output = 'target/site-motion';
const report = { checks: [], screenshots: [], exceptions: [] };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail === undefined ? {} : { detail }) });
let page, server, place = 'home';
const q = JSON.stringify;
const mirror = id => `document.querySelector('[data-omui-id="'+${q(id)}+'"]')`;
const snapshot = async (root, depth = -1) => page.value(`window.omui.snapshot({root:${q(root)},depth:${depth}}).then(r=>r.exitCode===0?r.data?.nodes:[])`);
const node = async id => (await snapshot(id, 0))?.find(n => n.id === id);
const scalar = async id => { const v = (await node(id))?.value; return v && typeof v === 'object' && 'value' in v ? v.value : v; };
const label = async id => (await node(id))?.name || '';
const exists = async id => !!await page.value(mirror(id));
const checked = async id => (await node(id))?.state?.includes('checked') || false;
const wait = async (predicate, timeout = 12000) => {
  const until = Date.now() + timeout;
  while (Date.now() < until) { if (await predicate()) return true; await sleep(80); }
  return false;
};
const requireCheck = async (predicate, what, detail) => {
  const ok = await wait(predicate); check(ok, what, ok ? undefined : typeof detail === 'function' ? await detail() : detail);
  if (!ok) throw new Error(what);
};
const shot = async name => {
  await sleep(200);
  const result = await page.call('Page.captureScreenshot', { format: 'png' });
  if (!result.result?.data) throw new Error('Screenshot failed: ' + name);
  const file = `${output}/${name}.png`;
  await writeFile(file, Buffer.from(result.result.data, 'base64')); report.screenshots.push(file);
};
async function scenario(name, run) {
  try { await run(); }
  catch (error) {
    check(false, name + ' completed', String(error.stack || error));
    try { await shot(name + '-failure'); } catch { /* Keep the original failure. */ }
  }
}
const dispatch = async (method, args) => {
  const result = await page.call(method, args); if (result.error) throw new Error(result.error.message);
};
async function visibleBounds(id) {
  for (let attempt = 0; attempt < 24; attempt++) {
    const n = await node(id);
    if (!n?.bounds || n.state?.includes('hidden')) { await sleep(80); continue; }
    const bounds = n.bounds;
    if (bounds.length !== 4 || !bounds.every(Number.isFinite) || bounds[2] <= 0 || bounds[3] <= 0) throw new Error('Invalid bounds: ' + id);
    const [x, y, w, h] = bounds;
    const viewport = id.startsWith('main/bar/') ? [0, 0, 1280, 800] : (await node('main/body/view/' + place))?.bounds;
    if (!viewport) { await sleep(80); continue; }
    const [vx, vy, vw, vh] = viewport;
    if (x + w / 2 >= vx && x + w / 2 <= vx + vw && y >= vy + 1 && y + h <= vy + vh - 1) return bounds;
    await dispatch('Input.dispatchMouseEvent', {
      type: 'mouseWheel', x: vx + vw * 0.8, y: vy + vh / 2, deltaX: 0,
      deltaY: y + h / 2 - (vy + vh / 2)
    });
    await sleep(160);
  }
  throw new Error('Target did not enter the live viewport: ' + id);
}
async function pointer(id, fraction = 0.5, moveOnly = false) {
  const [x, y, w, h] = await visibleBounds(id);
  const point = { x: x + w * fraction, y: y + h / 2 };
  await dispatch('Input.dispatchMouseEvent', { type: 'mouseMoved', ...point });
  if (moveOnly) return;
  for (const type of ['mousePressed', 'mouseReleased'])
    await dispatch('Input.dispatchMouseEvent', { type, ...point, button: 'left', clickCount: 1 });
  await sleep(80);
}
const key = async (name, code, virtual, modifiers = 0) => {
  for (const type of ['keyDown', 'keyUp']) await dispatch('Input.dispatchKeyEvent', { type, key: name, code, windowsVirtualKeyCode: virtual, modifiers });
  await sleep(50);
};
async function type(id, value) {
  await pointer(id);
  await requireCheck(() => page.value(`document.activeElement?.matches('textarea[data-omui="input"]')`), 'real input receives browser focus: ' + id);
  await key('a', 'KeyA', 65, 2);
  await dispatch('Input.insertText', { text: value });
  if (value === '') await key('Backspace', 'Backspace', 8);
  await requireCheck(async () => await scalar(id) === value, 'keyboard input reaches the actual field: ' + id, () => scalar(id));
}
async function segment(id, index, count, expected) {
  await pointer(id, (index + 0.5) / count);
  await requireCheck(async () => await scalar(id) === expected, 'pointer selects ' + expected, () => scalar(id));
}
async function core(destination) {
  const item = `main/bar/nav/core/menu/to_${destination}`;
  if (!(await node(item)) || (await node(item))?.state?.includes('hidden')) await pointer('main/bar/nav/core/open');
  await requireCheck(async () => !!await node(item) && !(await node(item))?.state?.includes('hidden'), 'Core menu exposes ' + destination);
  await pointer(item); place = destination;
  await requireCheck(() => exists(`main/body/view/${destination}/page`), 'Core menu opens ' + destination);
}
async function top(destination) {
  await pointer('main/bar/nav/to_' + destination); place = destination;
  await requireCheck(() => exists(`main/body/view/${destination}/page`), 'top navigation opens ' + destination);
}
const gallery = name => `main/body/view/${name}/page/motion_gallery`;
const choices = async root => (await snapshot(root + '/browse/choices'))?.filter(n => n.role === 'button' && n.id.startsWith(root + '/browse/choices/')) || [];
async function compactGallery(root, description) {
  await requireCheck(async () => {
    const nodes = await snapshot(root + '/transport');
    const buttons = (nodes || []).filter(n => n.role === 'button' && n.id.startsWith(root + '/transport/buttons/')).map(n => n.id.split('/').at(-1)).sort();
    const advanced = ['reverse', 'reset', 'state-a', 'state-b'].some(id => (nodes || []).some(n => n.id === root + '/transport/buttons/' + id));
    return JSON.stringify(buttons) === JSON.stringify(['details', 'play', 'try']) && !advanced && !(nodes || []).some(n => n.id === root + '/transport/seek');
  }, description + ': compact semantics expose exactly three toolbar buttons and no advanced controls');
  check(!await exists(root + '/browse/filters/search') && !await exists(root + '/browse/choices'), description + ': catalog starts unmounted');
}
async function openPicker(root) {
  if (!await exists(root + '/browse/filters/search')) await pointer(root + '/choose');
  await requireCheck(() => exists(root + '/browse/filters/search'), 'explicit Choose mounts the searchable catalog');
}
async function findDesign(root, id) {
  await openPicker(root);
  await type(root + '/browse/filters/search', id);
  await requireCheck(async () => (await choices(root)).length === 1 && (await choices(root))[0].id.endsWith('/' + id), 'search locates design: ' + id);
  const expected = await label(root + '/browse/choices/' + id);
  await pointer(root + '/browse/choices/' + id);
  await requireCheck(async () => expected.length > 0 && (await node(root + '/stage/scene'))?.name === expected, 'the selected scene has the chosen design semantic label: ' + id);
  await requireCheck(async () => !await exists(root + '/browse/filters/search') && !await exists(root + '/browse/choices'), 'choosing a design closes the catalog and returns to its working stage');
}
async function pauseTransport(root) {
  await visibleBounds(root + '/transport/buttons/play');
  if ((await label(root + '/transport/buttons/play')) === 'Pause') await pointer(root + '/transport/buttons/play');
  await requireCheck(async () => await label(root + '/transport/buttons/play') === 'Play', 'player exposes its paused state');
}

try {
  await mkdir(output, { recursive: true });
  if (!process.env.CHROME) throw new Error('CHROME must identify the browser executable');
  const arg = process.argv[2] === '--serve' ? process.argv[3] : process.argv[2];
  if (arg?.startsWith('http://') || arg?.startsWith('https://')) report.url = arg;
  else { server = await serve(arg || 'target/site'); report.url = server.url; }
  page = await browser(process.env.CHROME, { width: 1280, height: 800 }); report.browser = page.version;
  page.on((method, args) => { if (method === 'Runtime.exceptionThrown') report.exceptions.push(args.exceptionDetails?.exception?.description || args.exceptionDetails?.text || 'Runtime exception'); });
  await dispatch('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
  await dispatch('Browser.grantPermissions', { origin: new URL(report.url).origin, permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  await dispatch('Page.navigate', { url: report.url });
  const ready = await wait(() => page.value(`window.omuiStaticHandedOff && !!window.omui && !!${mirror('main/bar/nav/core/open')}`), 60000);
  check(ready, 'the real site hands off to the new live navigation'); if (!ready) throw new Error('The new live site did not become ready');

  await scenario('published-agent-policy', async () => {
    const description = await page.value('window.omui.describe()');
    const policy = description?.data?.agent_policy;
    check(description?.exitCode === 0 && policy?.mode === 'dual-user' && policy?.source === 'profile:site-web',
      'the delivered Web build selects its own published profile', policy);
    check(policy?.enabled === true && policy?.input === true && policy?.binding === 'instance' && policy?.session_valid === true,
      'published applications retain an instance-bound Agent endpoint and input');
    check(policy?.development_grant === false && policy?.development_controls === false,
      'the Web artifact does not inherit the native generator development session');
    const forged = await page.value(`window.omui.request({op:'describe',actor:'person:owner',dev:true,mode:'development'})`);
    check(forged?.exitCode === 0 && forged?.data?.agent_policy?.mode === 'dual-user' && forged?.data?.agent_policy?.development_grant === false,
      'remote actor and development claims do not upgrade the published policy');
    const input = await page.value(`window.omui.request({op:'input',actor:'agent:deployment-check',events:[{type:'click',node:'main/bar/nav/core/open'}]})`);
    check(input?.exitCode === 0, 'native Agent input completes through the delivered Wasm frame loop', input);
    check(input?.data?.actions?.some(a => a.node === 'main/bar/nav/core/open' && a.exitCode === 0 && a.actor === 'agent:deployment-check;via=service:input'),
      'the real input action records its Agent provenance', input?.data?.actions);
    await requireCheck(async () => {
      const item = await node('main/bar/nav/core/menu/to_motion');
      return !!item && !item.state?.includes('hidden');
    }, 'Agent input opens the same visible Core menu');
    await pointer('main/bar/nav/core/open');
    await requireCheck(async () => {
      const item = await node('main/bar/nav/core/menu/to_motion');
      return !item || item.state?.includes('hidden');
    }, 'a real person pointer closes that same menu without replacing the runtime');
  });

  await scenario('basic-motion', async () => {
    await core('motion'); const root = gallery('motion');
    await requireCheck(async () => parseInt(await label(root + '/heading/count'), 10) >= 40, 'basic catalog contains at least forty designs');
    check(!await exists(root + '/details/config'), 'motion configuration starts unmounted');
    await compactGallery(root, 'default basic motion');
    check((await node(root + '/stage/scene'))?.name === 'Shape the journey', 'the opening is the public triangle-to-circle path morph');
    check((await node(root + '/stage/scene'))?.bounds?.[3] <= 300.5, 'the default morph stays within a compact stage');
    const shape = root + '/stage/scene/composition/shape', change = root + '/stage/scene/composition/change';
    await pointer(root + '/stage', .5, true);
    const beforeShape = await label(shape);
    await pointer(change);
    await requireCheck(async () => await label(shape) !== beforeShape, 'pointer controls the actual morph endpoint');
    await key(' ', 'Space', 32);
    await requireCheck(async () => await label(shape) === beforeShape, 'keyboard controls that same morph endpoint');
    await pointer(root + '/transport/buttons/details');
    await requireCheck(() => exists(root + '/details/bend'), 'Details exposes the reusable path control');
    await pointer(root + '/details/bend'); await key('Home', 'Home', 36);
    await requireCheck(async () => Math.abs(await scalar(root + '/details/bend') + .3) < .001, 'keyboard sets a negative path bend');
    await pointer(root + '/details/copy');
    const copied = await page.value('navigator.clipboard.readText()');
    const decoded = JSON.parse(copied || '{}');
    check(Math.abs(decoded.vertex_path?.bend + .3) < .001, 'copied public motion configuration contains the real path parameter');
    await pointer(root + '/transport/buttons/details');
    await visibleBounds(root + '/stage'); await shot('shape-morph-opening');
    await openPicker(root);
    await requireCheck(async () => (await choices(root)).length === 8, 'catalog mounts eight choices per page');
    const first = (await choices(root)).map(n => n.id);
    await pointer(root + '/browse/pagination/next');
    await requireCheck(async () => (await label(root + '/browse/pagination/position')).startsWith('2 /'), 'a real Next click advances the catalog page');
    // Observe the replacement list itself, independently of its page label.
    await requireCheck(async () => {
      const current = await choices(root);
      return current.length === 8 && current.every(n => !first.includes(n.id));
    }, 'second page settles to eight different designs', async () => ({ first, current: (await choices(root)).map(n => n.id) }));
    await pointer(root + '/browse/pagination/previous');
    await findDesign(root, 'toggle-thumb'); await pauseTransport(root);
    await pointer(root + '/transport/buttons/details');
    await requireCheck(() => exists(root + '/details/speed'), 'explicit Details mounts playback parameters');
    await requireCheck(async () => {
      const nodes = await snapshot(root + '/transport');
      return ['/seek', '/buttons/reverse', '/buttons/reset', '/buttons/state-a', '/buttons/state-b'].every(suffix => (nodes || []).some(n => n.id === root + '/transport' + suffix));
    }, 'Details exposes seek, reverse, reset and both endpoint controls');
    if (await checked(root + '/details/settings/loop')) await pointer(root + '/details/settings/loop');
    await pointer(root + '/details/speed'); await key('Home', 'Home', 36);
    await requireCheck(async () => await scalar(root + '/details/speed') === 0.25, 'keyboard changes the real playback speed');
    // Playback intentionally stops offscreen. Bring the canvas back before
    // testing its clock; the nearby transport remains visible with it.
    await visibleBounds(root + '/stage');
    const setting = root + '/stage/scene/composition/setting/quiet';
    await pointer(root + '/transport/buttons/state-a');
    await requireCheck(async () => !!await node(setting) && !await checked(setting) && await scalar(root + '/transport/seek') === 0, 'A selects the real initial scene state');
    await pointer(root + '/transport/buttons/state-b');
    await requireCheck(async () => await checked(setting) && await scalar(root + '/transport/seek') === 1, 'B selects the real final scene state');
    await pointer(root + '/transport/buttons/reset');
    await requireCheck(async () => !!await node(setting) && !await checked(setting) && await scalar(root + '/transport/seek') === 0, 'Reset restores the real initial state and player position');
    await pointer(root + '/transport/seek', 0.75);
    await requireCheck(async () => { const v = await scalar(root + '/transport/seek'); return v > 0.6 && v < 0.9; }, 'pointer seeking selects an intermediate frame');
    const sought = await scalar(root + '/transport/seek'); await sleep(200);
    check(Math.abs(await scalar(root + '/transport/seek') - sought) < 0.01, 'the sought frame remains paused');
    await pointer(root + '/transport/buttons/reverse'); await pointer(root + '/transport/buttons/play');
    await requireCheck(async () => await scalar(root + '/transport/seek') < sought - 0.02, 'Reverse and Play move toward the previous frame');
    await pauseTransport(root);
    const stopped = await scalar(root + '/transport/seek'); await sleep(250);
    check(Math.abs(await scalar(root + '/transport/seek') - stopped) < 0.01, 'Pause stops the actual player clock');
    await pointer(root + '/transport/buttons/details');
    await requireCheck(async () => !await exists(root + '/details/config'), 'closing Details removes configuration');
    await compactGallery(root, 'closed motion details');
    await pointer(root + '/stage', 0.5, true);
    await requireCheck(async () => (await label(root + '/transport/hint')).includes('real interface'), 'moving the pointer into the canvas enters real interaction');
    const toggle = root + '/stage/scene/composition/setting/quiet', before = await checked(toggle);
    await pointer(toggle); await requireCheck(async () => await checked(toggle) !== before, 'the basic scene toggle changes real semantic state');
    await key(' ', 'Space', 32); await requireCheck(async () => await checked(toggle) === before, 'keyboard Space operates that same scene control');
    await shot('basic-motion-interactive');
  });

  await scenario('component-motion', async () => {
    await core('motion'); const root = gallery('motion');
    await findDesign(root, 'component-motion');
    check(!await exists(root + '/details/selection'), 'component motion opens with one stage and collapsed details');
    await pointer(root + '/transport/buttons/details');
    await requireCheck(() => exists(root + '/details/selection'), 'Details reveals reusable component animation selection');
    await pointer(root + '/transport/buttons/state-a');
    const card = root + '/stage/scene/composition/card';
    const atA = (await node(card)).bounds;
    await pointer(root + '/transport/buttons/state-b');
    await requireCheck(async () => Math.abs((await node(card)).bounds[0] - atA[0] - 24) < .5,
      'the actual component group translates by the public recipe');
    const invokeMotion = async (verb, args = {}) => {
      const reply = await page.value(`window.omui.invoke(${q('motion.' + verb)},${q(args)})`);
      await page.value('new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(() => resolve(true))))');
      return reply;
    };
    const discovered = await invokeMotion('inspect');
    check(discovered.exitCode === 0 && discovered.data?.available && discovered.data?.selected === 'component-motion',
      'delivered Web host exposes the actual public animation through a native semantic verb', discovered);
    for (const progress of [.25, .5, .75]) {
      const sought = await invokeMotion('seek', { progress });
      check(sought.exitCode === 0 && sought.data?.executed && Math.abs(sought.data?.progress - progress) < 1e-9,
        `Agent semantic seek samples progress ${progress}`, sought);
      await requireCheck(async () => Math.abs(await scalar(root + '/transport/seek') - progress) < .001,
        `human progress control observes Agent seek ${progress}`);
    }
    const atSeek = (await node(card)).bounds;
    const invalid = await invokeMotion('seek', { progress: 2 });
    check(invalid.exitCode !== 0 && Math.abs((await node(card)).bounds[0] - atSeek[0]) < .5,
      'invalid semantic progress preserves the live browser pose');
    await invokeMotion('seek', { progress: 1 });
    await pointer(root + '/transport/buttons/play');
    await requireCheck(async () => {
      const progress = await scalar(root + '/transport/seek');
      return progress > .05 && progress < .9;
    }, 'the native Hanim host advances intermediate frames in the actual browser');
    await pauseTransport(root);
    await pointer(root + '/details/selection'); await key('Home', 'Home', 36); await key('ArrowDown', 'ArrowDown', 40); await key('Enter', 'Enter', 13);
    await requireCheck(async () => await scalar(root + '/details/selection') === 'parts', 'keyboard selects independently animated parts');
    await pointer(root + '/transport/buttons/state-a');
    const titleA = (await node(card + '/title')).bounds;
    const partCardA = (await node(card)).bounds;
    await pointer(root + '/transport/buttons/state-b');
    await requireCheck(async () => Math.abs((await node(card)).bounds[0] - partCardA[0]) < .5 &&
      Math.abs((await node(card + '/title')).bounds[0] - titleA[0]) > 2,
      'part animation preserves the card layout while moving its selected title');
    await pointer(root + '/details/effect'); await key('End', 'End', 35); await key('Enter', 'Enter', 13);
    await requireCheck(async () => await scalar(root + '/details/effect') === 'layered', 'keyboard composes nested Hanim image effects on the real note');
    await pointer(root + '/transport/buttons/state-b');
    await shot('component-motion-nested-effects');
    await pointer(root + '/details/copy');
    const source = await page.value('navigator.clipboard.readText()');
    check(source.includes('hanim::parallel') && source.includes('.part="title"') && source.includes('playback.play') &&
      source.includes('.rotation_offset=') && source.includes('.scale_factor=') &&
      source.includes('hanim::composite') && source.includes('fx::blur') && source.includes('fx::tone'),
      'the browser clipboard receives the public C++ component recipe with relative part transforms');
    await type(card + '/editor', 'A browser-made note');
    await pointer(card + '/pin'); const pinned = await checked(card + '/pin');
    await pointer(card + '/actions/save');
    await requireCheck(() => page.value(`navigator.clipboard.readText().then(t=>{try{const n=JSON.parse(t);return n.title==='A browser-made note'&&n.pinned===${pinned}}catch{return false}})`),
      'real component input copies the edited note JSON to the browser clipboard');
    await key('Escape', 'Escape', 27);
    await requireCheck(async () => (await label(root + '/transport/hint')).includes('real interface'),
      'focus in the animated note keeps the stage in real interaction mode');
    await shot('component-motion-live-note');
    await pointer(root + '/details/effect'); await key('Home', 'Home', 36); await key('Enter', 'Enter', 13);
    await requireCheck(async () => await scalar(root + '/details/effect') === 'none' && await scalar(card + '/editor') === 'A browser-made note',
      'removing the effect preserves the actual edited component data');
    await pointer(root + '/details/selection'); await key('End', 'End', 35); await key('Enter', 'Enter', 13);
    await requireCheck(async () => await scalar(root + '/details/selection') === 'feedback', 'keyboard selects public feedback composition');
    await pointer(root + '/transport/buttons/state-a'); const feedbackA = (await node(card)).bounds;
    await pointer(root + '/transport/buttons/state-b');
    await requireCheck(async () => Math.abs((await node(card)).bounds[0] - feedbackA[0]) < .5, 'feedback returns the note to its starting position');
    await pointer(root + '/details/copy'); const feedbackSource = await page.value('navigator.clipboard.readText()');
    check(['pulse', 'shake', 'wobble'].every(name => feedbackSource.includes('hanim::anim::' + name)),
      'the browser copies the actual public feedback library calls');
    await pointer(root + '/transport/buttons/play');
    await requireCheck(async () => Math.abs((await node(card)).bounds[0] - feedbackA[0]) > 2, 'public feedback moves through an actual intermediate frame');
    await pauseTransport(root); await shot('component-motion-feedback');
    await pointer(root + '/transport/buttons/details');
    await requireCheck(async () => !await exists(root + '/details/selection'), 'closing Details removes the component recipe controls');
  });

  await scenario('creative-demos', async () => {
    await top('explore'); const root = gallery('explore');
    await requireCheck(async () => parseInt(await label(root + '/heading/count'), 10) >= 20, 'creative catalog contains at least twenty designs');
    await compactGallery(root, 'default creative demo');
    check(!!await node(root + '/stage/scene/composition/workspace/swatches/swatch-0/hex'), 'creative demos open the useful palette first');
    check((await node(root + '/stage/scene'))?.bounds?.[3] <= 300.5, 'the default palette stage stays within 300 pixels');
    await openPicker(root);
    check((await choices(root)).length === 8, 'creative catalog also limits mounted choices to eight');
    await findDesign(root, 'packing-board');
    const work = root + '/stage/scene/composition/workspace';
    await pointer(root + '/stage', 0.5, true); await pointer(work + '/pack');
    await requireCheck(async () => (await snapshot(work + '/items'))?.filter(n => n.role === 'checkbox' && n.state?.includes('checked')).length === 3, 'packing-board really packs all three stable items');
    await shot('creative-packing');
    await findDesign(root, 'palette-studio'); await pointer(root + '/stage', 0.5, true);
    const hexes = async () => Promise.all([0, 1, 2, 3].map(i => label(work + '/swatches/swatch-' + i + '/hex')));
    const originalColours = await hexes();
    const mix = work + '/mix'; await pointer(mix, 0.8);
    const before = await scalar(mix); await key('ArrowLeft', 'ArrowLeft', 37);
    await requireCheck(async () => await scalar(mix) < before, 'palette-studio accepts a real slider keyboard adjustment');
    check((await label(work + '/actions/amount')).includes('%'), 'the palette shows its current real balance');
    await requireCheck(async () => { const colours = await hexes(); return colours.length === 4 && colours.every((hex, i) => /^#[0-9a-f]{6}$/i.test(hex) && hex !== originalColours[i]) && new Set(colours).size === 4; }, 'contrast changes all four readable, distinct colour outputs');
    const colours = await hexes();
    await pointer(work + '/actions/copy');
    await requireCheck(() => page.value(`navigator.clipboard.readText().then(t=>t.startsWith(':root {')&&${q(colours)}.every(hex=>t.includes(hex))&&['light','soft','ink','shade'].every(name=>t.includes('--palette-'+name+':')))`), 'Copy palette puts usable CSS with the current four colours on the actual browser clipboard');
    await shot('creative-palette');
  });

  await scenario('agent-native', async () => {
    await top('agent'); const root = 'main/body/view/agent/page/agent_feature';
    await requireCheck(async () => await scalar(root + '/modes') === 'use', 'agent opens the Use path');
    check((await label(root + '/use/label')).includes('Simulated'), 'the demonstration is explicitly identified as simulated');
    await pointer(root + '/use/actions/play');
    await requireCheck(async () => { const stop = await node(root + '/use/actions/stop'); return !!stop && !stop.state?.includes('disabled'); }, 'the simulation offers a real Stop control');
    await pointer(root + '/use/actions/stop'); await sleep(1500);
    check(!(await label(root + '/specimen/receipt')).includes('Recorded here'), 'Stop cancels the pending local record');
    await pointer(root + '/use/actions/play');
    await requireCheck(async () => (await label(root + '/specimen/receipt')).includes('Recorded here'), 'the explicitly started agent completes real task actions');
    check((await snapshot(root + '/use/log'))?.length > 2, 'the agent action log contains the actual task records');
    await pointer(root + '/history/undo');
    await requireCheck(async () => !(await label(root + '/specimen/receipt')).includes('Recorded here'), 'the real task action can be undone');
    await segment(root + '/modes', 1, 3, 'develop');
    await requireCheck(() => exists(root + '/preview/actions/verify'), 'Develop exposes its stable Verify action');
    check(!await exists(root + '/preview/actions/run'), 'different task meanings do not share the legacy Run identity');
    await pointer(root + '/preview/actions/verify');
    await requireCheck(async () => (await label(root + '/status')).startsWith('PASS'), 'the real Verify primary runs read, change and check');
    check(!await exists(root + '/develop/expected'), 'development parameters start unmounted');
    await pointer(root + '/method/toggle');
    await requireCheck(() => exists(root + '/develop/expected'), 'explicit Agent details mounts protocol controls');
    await type(root + '/develop/expected', 'Browser verified idea');
    await pointer(root + '/develop/requests/assert');
    await requireCheck(async () => (await label(root + '/status')).startsWith('FAIL'), 'Assert genuinely fails before the requested change');
    await pointer(root + '/develop/requests/read');
    await requireCheck(async () => (await label(root + '/status')).includes('semantic subtree'), 'Read reports the actual runtime snapshot');
    await pointer(root + '/develop/requests/change'); await pointer(root + '/develop/requests/assert');
    await requireCheck(async () => (await label(root + '/status')).startsWith('PASS') && await scalar(root + '/specimen/title') === 'Browser verified idea', 'Change then Assert passes on the actual current value');
    await segment(root + '/modes', 2, 3, 'design');
    await requireCheck(() => exists(root + '/preview/actions/arrange'), 'Design exposes its distinct Arrange action');
    await pointer(root + '/specimen/title', 0.5, true);
    const previousCard = await node(root + '/specimen/decision_card');
    const beforeCard = !!previousCard && !previousCard.state?.includes('removing');
    await pointer(root + '/preview/actions/arrange');
    await requireCheck(async () => { const card = await node(root + '/specimen/decision_card'); return (!!card && !card.state?.includes('removing')) !== beforeCard; }, 'the real Arrange primary changes this task grouping');
    check(!await exists(root + '/design/spacing'), 'design parameters start unmounted');
    await pointer(root + '/method/toggle');
    await requireCheck(() => exists(root + '/design/spacing'), 'explicit Agent details mounts design parameters');
    await segment(root + '/design/layout', 1, 2, 'cards');
    await pointer(root + '/design/spacing'); await key('End', 'End', 35);
    await requireCheck(async () => await scalar(root + '/design/spacing') === 24, 'Design changes the live specimen spacing');
    check(!!await node(root + '/specimen/record'), 'design layout preserves its action address');
    await pointer(root + '/design/copy_prompt');
    await requireCheck(() => page.value(`navigator.clipboard.readText().then(t=>t.includes('"origin":"user_content"')&&t.includes('Browser verified idea')&&t.includes('"spacing":24')&&t.includes('agent-design'))`), 'the real clipboard receives the design prompt with current constraints and quoted user data');
    await shot('agent-design');
  });

  await scenario('unified-interface', async () => {
    await core('interface'); const root = 'main/body/view/interface/page/interface_feature', object = root + '/workspace/object';
    check(!await exists(root + '/details/language') && !await exists(root + '/details/source'), 'interface languages and source start unmounted');
    await type(object + '/title', 'Same object in browser');
    const ids = (await snapshot(object)).map(n => n.id).sort();
    await requireCheck(async () => (await label(root + '/workspace/meaning/title')).includes('Same object in browser'), 'the live semantic view reads back the edited object');
    await pointer(root + '/toggle_details');
    await requireCheck(() => exists(root + '/details/language'), 'explicit Details mounts the language picker');
    for (const [index, language, spelling] of [[0, 'cpp', 'text_box('], [1, 'ts', 'textBox('], [2, 'python', 'omui.text_box('], [3, 'lua', 'omui.TextBox']]) {
      await segment(root + '/details/language', index, 4, language);
      await requireCheck(async () => (await label(root + '/details/source')).includes(spelling) && (await label(root + '/details/source')).includes('Same object in browser'), language + ' displays its existing builder API expression with the current value');
    }
    check(JSON.stringify((await snapshot(object)).map(n => n.id).sort()) === JSON.stringify(ids) && await scalar(object + '/title') === 'Same object in browser', 'changing source language preserves the same object IDs and model');
    const source = await label(root + '/details/source'); await pointer(root + '/details/copy');
    await requireCheck(() => page.value(`navigator.clipboard.readText().then(t=>t===${q(source)})`), 'the browser clipboard receives the displayed current API expressions');
    await shot('interface-language-details');
  });

  await scenario('scoped-styling', async () => {
    await core('styles'); const root = 'main/body/view/styles/page/styles_feature';
    check(!await exists(root + '/details/radius') && !await exists(root + '/details/scope'), 'style parameters and source start unmounted');
    await pointer(root + '/preview_controls/preview_pause');
    await requireCheck(async () => (await label(root + '/preview_controls/preview_pause')).includes('Resume'), 'the actual preview Pause control pauses automatic changes');
    const left = root + '/comparison/default/controls/quiet', right = root + '/comparison/styled/controls/quiet', before = await checked(left);
    await pointer(left);
    await requireCheck(async () => await checked(left) !== before && await checked(right) === await checked(left), 'the default and scoped styles operate the same real model');
    check((await node(root + '/comparison/styled/backdrop/chart'))?.role === 'canvas', 'the scoped glass controls sit over a real session data chart');
    const quietBeforeTabs = await checked(left);
    await pointer(root + '/comparison/styled/controls/lens/' + (quietBeforeTabs ? 'on' : 'off'));
    await requireCheck(async () => await checked(left) !== quietBeforeTabs && await checked(right) === await checked(left), 'the glass notification tabs operate that same quiet setting');
    await pointer(root + '/actions/details');
    await requireCheck(() => exists(root + '/details/radius'), 'explicit Details mounts style parameters');
    await pointer(root + '/details/radius'); await key('End', 'End', 35);
    await requireCheck(async () => await scalar(root + '/details/radius') === 28, 'keyboard input changes the real sample corner parameter');
    await shot('scoped-styles-details');
  });
  check(report.exceptions.length === 0, 'no browser runtime exceptions occurred', report.exceptions);
} catch (error) { check(false, 'browser acceptance started and completed', String(error.stack || error)); }
finally { page?.close(); server?.close(); }
report.ok = report.checks.length > 0 && report.checks.every(c => c.ok);
await writeFile(output + '/report.json', JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify(report, null, 2)); process.exit(report.ok ? 0 : 1);
