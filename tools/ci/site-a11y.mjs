// The site as assistive technology meets it in a real browser: headless
// Chromium's own accessibility tree — what a screen reader on the page is
// given (Accessibility.getFullAXTree) — read once the program runs, and the
// keyboard's way in. Checked:
//
// - the landmarks: the places a navigation landmark, the place shown the main one;
// - a level-1 heading, its level the browser's;
// - every button and link named;
// - the page's language, and zoom allowed (WCAG 1.4.4);
// - the static page read before the program hidden once it runs;
// - the skip link (WCAG 2.4.1): the first Tab stop, seen while focused;
//   Enter takes the focus into the main landmark (the program's focus and
//   the page's both).
//
// Prints a JSON report; the exit code is 0 when every check holds.
//
//   CHROME=<chromium> node tools/ci/site-a11y.mjs (<url> | --serve DIR) [--width W] [--height H]
import { browser, serve, sleep, usage } from './page.mjs';

const args = process.argv.slice(2);
const opt = (name, fallback) => { const i = args.indexOf(`--${name}`); return i >= 0 ? args[i + 1] : fallback; };
const w = +opt('width', 1280), h = +opt('height', 800), dir = opt('serve', '');
const chrome = process.env.CHROME;
let url = args[0] && !args[0].startsWith('--') ? args[0] : '';
if (!chrome || (!url && !dir)) usage('site-a11y.mjs', ' [--width W] [--height H]');
const server = dir ? await serve(dir) : null;
if (server) url = server.url;

const b = await browser(chrome, { width: w, height: h });
const report = { url, browser: b.version, checks: [] };
const check = (ok, what) => { report.checks.push({ ok: !!ok, what }); return ok; };
await b.call('Page.navigate', { url });
// Running: window.omui answers and the mirror is filled.
for (let i = 0; i < 300; i++) {
  if (await b.value(`!!window.omui && (document.querySelector('[data-omui=mirror]')?.childElementCount ?? 0) > 0`)) break;
  await sleep(100);
}
await sleep(2500);  // the opening at rest

await b.call('Accessibility.enable');
const { nodes = [] } = (await b.call('Accessibility.getFullAXTree')).result ?? {};
const seen = nodes.filter((n) => !n.ignored);
const role = (n) => n.role?.value ?? '';
const name = (n) => n.name?.value ?? '';
const prop = (n, p) => n.properties?.find((x) => x.name === p)?.value?.value;
report.tree = { nodes: seen.length, roles: Object.fromEntries(Object.entries(seen.reduce((m, n) => ((m[role(n)] = (m[role(n)] ?? 0) + 1), m), {})).sort()) };
const nav = seen.find((n) => role(n) === 'navigation'), main = seen.find((n) => role(n) === 'main');
check(nav && name(nav) === 'Places', `a navigation landmark, named (${nav ? JSON.stringify(name(nav)) : 'none'})`);
check(main, `a main landmark (${main ? JSON.stringify(name(main)) : 'none'})`);
const h1 = seen.find((n) => role(n) === 'heading' && prop(n, 'level') === 1);
check(h1, `a level-1 heading (${h1 ? JSON.stringify(name(h1)) : 'none'})`);
const unnamed = seen.filter((n) => (role(n) === 'button' || role(n) === 'link') && !name(n).trim());
check(unnamed.length === 0, `every button and link named (${unnamed.length} not)`);
report.headings = seen.filter((n) => role(n) === 'heading').map((n) => ({ level: prop(n, 'level'), name: name(n) })).slice(0, 12);

const page = await b.value(`({ lang: document.documentElement.lang,
  viewport: document.querySelector('meta[name=viewport]')?.content ?? '',
  statik: document.getElementById('omui-static')?.hidden ?? null })`);
check(page.lang, `the page's language (${page.lang})`);
check(!/user-scalable\s*=\s*no|maximum-scale\s*=\s*1(\D|$)/.test(page.viewport), `zoom allowed (${page.viewport})`);
check(page.statik === true, 'the static page hidden once the program runs');

// The implementation's identity, including the browser tab: the generated
// icon is the same font-free original, rather than a separately drawn asset.
report.identity = await b.value(`(() => {
  const href = document.querySelector('link[rel="icon"]')?.getAttribute('href') ?? '';
  const svg = href.startsWith('data:image/svg+xml,')
    ? new DOMParser().parseFromString(decodeURIComponent(href.slice(href.indexOf(',') + 1)), 'image/svg+xml') : null;
  return { title: document.title, icon: svg?.querySelector('title')?.textContent ?? '',
    paths: svg?.querySelectorAll('path').length ?? 0, external: svg?.querySelectorAll('text,image').length ?? 0,
    errors: svg?.querySelectorAll('parsererror').length ?? 1 };
})()`);
check(report.identity.title.startsWith('omui'), 'the implementation is named omui in the browser tab');
check(report.identity.icon === 'omui' && report.identity.paths === 5 && report.identity.external === 0 && report.identity.errors === 0,
  'the tab icon is the generated O and inner letters, with no font or image dependencies');

// The keyboard's way in: Tab from the top of the page, then Enter.
const key = async (k, code, vk) => {
  await b.call('Input.dispatchKeyEvent', { type: 'keyDown', key: k, code, windowsVirtualKeyCode: vk });
  await b.call('Input.dispatchKeyEvent', { type: 'keyUp', key: k, code, windowsVirtualKeyCode: vk });
};
await b.value(`document.activeElement && document.activeElement.blur && document.activeElement.blur(), true`);
await key('Tab', 'Tab', 9);
await sleep(200);
const first = await b.value(`(() => { const a = document.activeElement, r = a.getBoundingClientRect();
  return { omui: a.getAttribute('data-omui'), text: a.textContent, top: r.top, height: r.height }; })()`);
report.first_stop = first;
check(first.omui === 'skip', `the first Tab stop is the skip link (${first.omui ?? first.text})`);
check(first.top >= 0 && first.height > 0, 'seen while focused');
await key('Enter', 'Enter', 13);
await sleep(800);
const after = await b.value(`(() => { const a = document.activeElement; return { id: a.getAttribute('data-omui-id'), tag: a.tagName }; })()`);
const focused = await b.value(`window.omui.find({ state: ['focused'] }).then((r) => { const d = r.data; const list = Array.isArray(d) ? d : (d?.nodes ?? []); return list.map((n) => n.id); })`);
report.after_skip = { page: after, program: focused };
const inMain = (id) => typeof id === 'string' && id.startsWith('main/body/view/');
check(inMain(after.id), `Enter: the page's focus in the main landmark (${after.id ?? after.tag})`);
check((focused ?? []).some(inMain), `and the program's (${JSON.stringify(focused)})`);

// Follow the visible Explore link and then open its brand program with real clicks.
const clickNode = async (id) => {
  await b.value(`window.omui.act(${JSON.stringify(id)},'scroll_into_view')`);
  await sleep(400);
  const n = await b.value(`window.omui.snapshot({root:${JSON.stringify(id)},depth:0}).then(r => (r.data?.nodes ?? []).find(n => n.id === ${JSON.stringify(id)}))`);
  if (n?.bounds?.length !== 4) return false;
  const [x, y, w, h] = n.bounds;
  for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased'])
    await b.call('Input.dispatchMouseEvent', { type, x: x + w / 2, y: y + h / 2, button: 'left', clickCount: 1 });
  await sleep(1200);
  return true;
};
await clickNode('main/bar/nav/to_explore');
await clickNode('main/body/view/explore/page/explore/works/brand/open');
const study = await b.value(`window.omui.snapshot({root:'main/body/view/brand/page/identity/title',depth:0}).then(r =>
  (r.data?.nodes ?? []).find(n => n.id.endsWith('/identity/title'))?.name ?? '')`);
check(study === 'A mark. Made here.', 'Explore opens the live brand study with real clicks');
await clickNode('main/bar/mark');
check(await b.value(`document.querySelector('[data-omui-id="main/body/view/home/page/landing"]') !== null`), 'the brand link returns home');

report.ok = report.checks.every((c) => c.ok);
console.log(JSON.stringify(report, null, 2));
await b.close();
if (server) server.close();
process.exit(report.ok ? 0 : 1);
