// The component gallery as delivered: static deep links, pointer navigation,
// real state, history, clipboard feedback and a phone's live detail.
import { mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';
const server = await serve(process.argv[2] || 'target/site');
const b = await browser(process.env.CHROME, { width: 1280, height: 800 });
const report = { checks: [] };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail ? { detail } : {}) });
const until = async (expression, timeout = 30000) => {
  for (let i = 0; i < timeout / 100; i++) { if (await b.value(expression)) return true; await sleep(100); }
  return false;
};
const root = 'main/body/view/components/page/gallery';
const overview = root + '/overview';
const detail = id => root + '/detail/' + id;
const node = id => `document.querySelector('[data-omui-id="${id}"]')`;
const name = id => `(${node(id)}?.getAttribute('aria-label') ?? ${node(id)}?.textContent)`;
const act = (id, action, args = {}) => b.value(`window.omui.act(${JSON.stringify(id)},${JSON.stringify(action)},{args:${JSON.stringify(args)}})`);
const pointer = async id => {
  // Layout and disclosure decide where an action lives. Reach the actual
  // control before sending physical pointer input; do not pin scroll offsets.
  await act(id, 'scroll_into_view');
  await sleep(250);
  const bounds = await b.value(`window.omui.snapshot({root:${JSON.stringify(id)},depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)`);
  if (!Array.isArray(bounds) || bounds.length !== 4 || !bounds.every(Number.isFinite)) throw new Error('Invalid bounds: ' + id);
  const [x, y, width, height] = bounds;
  for (const type of ['mouseMoved', 'mousePressed', 'mouseReleased']) {
    const result = await b.call('Input.dispatchMouseEvent', { type, x: x + width / 2, y: y + height / 2, button: 'left', clickCount: 1 });
    if (result.error) throw new Error(result.error.message);
  }
};
const shot = async name => {
  await sleep(400);
  const png = await b.call('Page.captureScreenshot', { format: 'png' });
  await writeFile('target/site-catalog/' + name + '.png', Buffer.from(png.result.data, 'base64'));
};
try {
  await mkdir('target/site-catalog', { recursive: true });
  await b.call('Emulation.setScriptExecutionDisabled', { value: true });
  for (const route of ['components/text-box', 'zh/components/data-grid', 'zh/components/code_editor']) {
    await b.call('Page.navigate', { url: server.url + '#/' + route }); await sleep(300);
    const shown = await b.value(`Array.from(document.querySelectorAll('#omui-static section[data-place]')).filter(n=>getComputedStyle(n).display!=='none').map(n=>({id:n.id,title:n.querySelector('h1')?.textContent}))`);
    check(shown?.length === 1 && shown[0].title && shown[0].id.includes('components/'), 'static component destination: ' + route, shown);
  }
  await b.call('Emulation.setScriptExecutionDisabled', { value: false });
  await b.call('Page.navigate', { url: server.url + '?catalog=1#/components/text-box' });
  const handedOff = await until(`window.omuiStaticHandedOff && ${node(detail('text_box') + '/stage/specimen/variants/name')}`);
  check(handedOff, 'a direct detail hands off to its own live specimen');
  if (!handedOff) throw new Error('The requested live detail did not appear');
  const origin = await b.value('performance.timeOrigin');
  await act(detail('text_box') + '/stage/specimen/variants/name', 'set_value', { value: 'Lin' });
  await act(detail('text_box') + '/panes/code', 'activate');
  check(await until(`${name(detail('text_box') + '/code/scroll/source')}?.includes('Lin')`), 'builder reference follows the real input value');
  for (const [language, prefix] of [['ts', 'textBox('], ['python', 'omui.text_box('], ['lua', 'omui.TextBox {'], ['cpp', 'text_box(']]) {
    await act(detail('text_box') + '/code/language/' + language, 'activate');
    check(await until(`${name(detail('text_box') + '/code/scroll/source')}?.startsWith(${JSON.stringify(prefix)}) && ${name(detail('text_box') + '/code/scroll/source')}?.includes('Lin')`), language + ' expression describes the same edited model');
  }
  await act(detail('text_box') + '/stage/actions/reset', 'activate');
  check(await until(`${name(detail('text_box') + '/code/scroll/source')}?.includes('Ada')`), 'reset restores the specimen and reference');
  await b.call('Browser.grantPermissions', { origin: server.url.slice(0, -1), permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'] });
  await pointer(detail('text_box') + '/code/copy');
  check(await until(`${name(detail('text_box') + '/code/copied')}==='Expression copied'`), 'pointer copy shows completion feedback');
  const source = await b.value(name(detail('text_box') + '/code/scroll/source'));
  // The host's writeText is asynchronous; label feedback is not its completion.
  const copied = source && await until(`navigator.clipboard.readText().then(t=>t===${JSON.stringify(source)})`);
  check(copied, 'the permitted browser clipboard contains the displayed expression', copied ? undefined : { source, clipboard: await b.value('navigator.clipboard.readText().catch(e=>String(e))') });
  await act('main/body/view/components', 'scroll_to', { x: 0, y: 0 });
  await act(root + '/detail/directory/all', 'activate');
  check(await until(`location.hash==='#/components' && ${name(overview + '/results/count')}==='8 / 36'`), 'overview offers eight directory choices beside the live specimen');
  const preview = await b.value(`window.omui.snapshot({root:${JSON.stringify(root)}}).then(r=>(r.data?.nodes||[]).filter(n=>n.id.endsWith('/stage/specimen')).map(n=>n.id))`);
  check(preview?.length === 1, 'the bounded directory does not stack complete specimens', preview);
  const results = await b.value(`window.omui.snapshot({root:${JSON.stringify(overview + '/results')},depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)`);
  check(Array.isArray(results) && results[3] <= 331, 'desktop component choices stay inside a bounded scroll region', results);
  if (preview?.length !== 1) throw new Error('The directory needs one live specimen');
  const active = preview[0].slice(0, -'/stage/specimen'.length);
  check(!await b.value(node(active + '/panes/code')), 'the preview starts outside the language and source reference');
  await pointer(active + '/stage/actions/pause');
  check(await until(`${name(active + '/stage/actions/pause')}==='Play preview'`), 'the real preview can be paused before inspecting a fixed state');
  await shot('overview-en-desktop');
  await act(overview + '/filter/category', 'set_value', { value: 'editing' });
  check(await until(`${name(overview + '/results/count')}==='2 / 36'`), 'the editing category contains its two components');
  await act(overview + '/filter/search', 'set_value', { value: 'no-such-part' });
  check(await until(`${node(overview + '/results/empty')}`), 'an empty filtered search offers a recovery path');
  check(preview?.length === 1 && await b.value(node(preview[0])), 'an empty directory preserves the current real specimen');
  await act(overview + '/filter/clear', 'activate');
  check(await until(`${name(overview + '/results/count')}==='8 / 36'`), 'reset restores the selected overview');
  await act(overview + '/filter/search', 'set_value', { value: 'button' });
  check(await until(`${name(overview + '/results/count')}==='1 / 36'`), 'search finds a component by name');
  await pointer(overview + '/results/r0/button/heading/open');
  check(await until(`location.hash==='#/components' && ${node(detail('button') + '/stage/specimen/variants/primary')} && !${node(detail('button') + '/panes/code')}`), 'a real directory click selects the preview without navigating into source');
  await pointer(detail('button') + '/stage/actions/details');
  check(await until(`location.hash==='#/components/button' && ${node(detail('button') + '/panes/code')}`), 'only View details opens the addressable component workspace');
  // A default preview may already have demonstrated a state change. Establish
  // the actual initial model with its real reset control before testing a click.
  await pointer(detail('button') + '/stage/actions/reset');
  check(await until(`${name(detail('button') + '/stage/specimen/variants/primary')}==='Save'`), 'reset establishes the real initial model after automatic preview');
  await pointer(detail('button') + '/stage/specimen/variants/primary');
  check(await until(`${name(detail('button') + '/stage/specimen/variants/primary')}==='Saved 1'`), 'the sample button performs its own action');
  await pointer(detail('button') + '/stage/actions/reset');
  check(await until(`${name(detail('button') + '/stage/specimen/variants/primary')}==='Save'`), 'a pointer can reset the specimen');
  await shot('button-en-desktop');
  await b.value('history.back(); true');
  check(await until(`location.hash==='#/components' && ${node(overview)} && ${name(overview + '/filter/search')}==='Search components' && !${node(detail('button') + '/panes/code')}`), 'Back returns to the catalog preview');
  check(await b.value(`${name(overview + '/results/count')}`) === '1 / 36', 'catalog search survives the detail visit');
  await b.value('history.forward(); true');
  check(await until(`location.hash==='#/components/button' && ${node(detail('button') + '/stage/specimen/variants/primary')} && ${node(detail('button') + '/panes/code')}`), 'Forward restores the selected component detail');
  check(await b.value('performance.timeOrigin') === origin, 'component history keeps the runtime alive');
  await b.call('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: true });
  await b.value(`location.hash='/zh/components/text-box'; true`);
  check(await until(`${name(detail('text_box') + '/title')}==='文本框'`), 'Chinese phone opens the same component address');
  await shot('text-box-zh-phone');
  const inside = await b.value(`window.omui.snapshot({root:${JSON.stringify(detail('text_box'))}}).then(r=>{const controls=(r.data?.nodes||[]).filter(n=>['button','text_box'].includes(n.role)&&n.bounds&&!n.state?.includes('hidden')&&!n.state?.includes('offscreen'));return controls.length>2&&controls.every(n=>n.bounds[0]>=-0.5&&n.bounds[0]+n.bounds[2]<=390.5)})`);
  check(inside, 'the phone detail keeps its controls inside the viewport');
  check(!await b.value(node(overview + '/filter/search')), 'the phone directory starts collapsed');
  await pointer(overview + '/browse');
  check(await until(`${node(overview + '/filter/search')}`), 'a phone reveals its searchable directory on demand');
  const phoneResults = await b.value(`window.omui.snapshot({root:${JSON.stringify(overview + '/results')},depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)`);
  check(Array.isArray(phoneResults) && phoneResults[3] <= 231, 'the phone directory also has a bounded height', phoneResults);
  await act(overview + '/filter/search', 'set_value', { value: 'text_box' });
  check(await until(`${name(overview + '/results/count')}==='1 / 36'`), 'the phone searches the full catalog');
  await pointer(overview + '/results/r0/text_box/heading/open');
  check(await until(`location.hash==='#/zh/components' && ${node(detail('text_box') + '/stage/specimen/variants/name')} && !${node(overview + '/filter/search')}`), 'choosing a phone specimen closes its directory and returns room to the live preview');
  check(!await b.value(node(detail('text_box') + '/panes/code')), 'phone selection also keeps deeper controls behind View details');
  await pointer(detail('text_box') + '/stage/actions/details');
  check(await until(`location.hash==='#/zh/components/text-box' && ${node(detail('text_box') + '/panes/code')}`), 'the phone explicitly opens the same addressable detail');
  await shot('text-box-zh-phone-detail');
} catch (error) { check(false, 'browser catalog scenario completed', String(error.stack || error)); }
finally { await b.close(); server.close(); }
report.ok = report.checks.every(c => c.ok);
console.log(JSON.stringify(report, null, 2));
process.exit(report.ok ? 0 : 1);
