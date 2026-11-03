// Human input and semantic actions on the shared release workspace.
import { mkdir, writeFile } from 'node:fs/promises';
import { browser, serve, sleep } from './page.mjs';
const server = await serve(process.argv[2] || 'target/site');
const b = await browser(process.env.CHROME, { width: 1280, height: 800 });
const report = { checks: [] };
const check = (ok, what, detail) => report.checks.push({ ok: !!ok, what, ...(detail ? { detail } : {}) });
const until = async expression => { for (let i = 0; i < 300; i++) { if (await b.value(expression)) return true; await sleep(100); } return false; };
const hero = 'main/body/view/release/page/hero', board = hero + '/demo/specimen', motion = 'main/body/view/release/page/motion/shown';
const node = id => `document.querySelector('[data-omui-id="${id}"]')`;
const name = id => `(${node(id)}?.getAttribute('aria-label') ?? ${node(id)}?.textContent)`;
const act = (id, action, args = {}) => b.value(`window.omui.act(${JSON.stringify(id)},${JSON.stringify(action)},{args:${JSON.stringify(args)}})`);
const intoView = async (id, viewport = 'main/body/view/release') => {
  for (let i = 0; i < 20; i++) {
    const [bounds, view] = await b.value(`Promise.all([${JSON.stringify(id)},${JSON.stringify(viewport)}].map(root=>window.omui.snapshot({root,depth:0}).then(r=>r.data?.nodes?.[0]?.bounds)))`);
    // A preceding click can mount this control on the next rendered frame.
    if (!Array.isArray(bounds) || !Array.isArray(view)) { await sleep(100); continue; }
    if (!bounds.every(Number.isFinite)) throw new Error('Invalid bounds: ' + id);
    if (bounds[1] >= view[1] && bounds[1] + bounds[3] <= view[1] + view[3]) return bounds;
    // A scroll-linked layout may move again after scroll_into_view. A
    // person's wheel brings the target to the middle of the live viewport.
    await b.call('Input.dispatchMouseEvent', { type: 'mouseWheel', x: view[0] + view[2] / 2, y: view[1] + view[3] / 2, deltaX: 0, deltaY: bounds[1] + bounds[3] / 2 - view[1] - view[3] / 2 });
    await sleep(300);
  }
  throw new Error('The control did not enter the live viewport: ' + id);
};
const click = async (id, viewport) => {
  const bounds = await intoView(id, viewport);
  const [x, y, width, height] = bounds;
  for (const type of ['mouseMoved','mousePressed','mouseReleased']) {
    const r = await b.call('Input.dispatchMouseEvent', { type, x: x + width / 2, y: y + height / 2, button: 'left', clickCount: 1 });
    if (r.error) throw new Error(r.error.message);
  }
};
const shot = async name => {
  await sleep(400); const png = await b.call('Page.captureScreenshot', { format: 'png' });
  await writeFile('target/site-workspace/' + name + '.png', Buffer.from(png.result.data,'base64'));
};
try {
  await mkdir('target/site-workspace', { recursive: true });
  await b.call('Page.navigate', { url: server.url + '#/scenes/release' });
  const ready = await until(`window.omuiStaticHandedOff && ${node(board + '/add/title')}`);
  check(ready, 'the release route opens the live workspace'); if (!ready) throw new Error('Workspace did not become ready');
  check(!await b.value(node(hero + '/demo/renderings')), 'technical views start closed');
  await shot('home-en-desktop');
  await click(board + '/add/title');
  if (!await until(`document.activeElement?.matches('textarea[data-omui="input"]')`)) throw new Error('The text input did not acquire browser focus');
  await b.call('Input.insertText', { text: 'Review the keyboard path' });
  check(await until(`window.omui.snapshot({root:${JSON.stringify(board + '/add/title')},depth:0}).then(r=>r.data?.nodes?.[0]?.value==='Review the keyboard path')`), 'real text input reaches the draft');
  await click(board + '/add/submit');
  check(await until(`${name(board + '/items/task-1/done')}==='Review the keyboard path'`), 'a pointer adds a task with its own stable id');
  check(await until(`${name(motion + '/shape/task-1/title')}==='Review the keyboard path'`), 'the motion scene reads that same task');
  await click(board + '/items/task-1/edit');
  check(await until(`window.omui.snapshot({root:${JSON.stringify(board + '/editor/title')},depth:0}).then(r=>r.data?.nodes?.[0]?.state?.includes('focused'))`), 'editing focuses the real title field');
  if (!await until(`document.activeElement?.matches('textarea[data-omui="input"]')`)) throw new Error('The editor did not acquire browser focus');
  for (const type of ['keyDown','keyUp']) await b.call('Input.dispatchKeyEvent',{type,key:'a',code:'KeyA',windowsVirtualKeyCode:65,modifiers:2});
  await b.call('Input.insertText',{text:'Review keyboard and focus'});
  await click(board + '/editor/actions/save');
  check(await until(`${name(board + '/items/task-1/done')}==='Review keyboard and focus'`), 'rename updates the live task');
  await click(board + '/history/undo');
  check(await until(`${name(board + '/items/task-1/done')}==='Review the keyboard path'`), 'workspace undo restores the previous title');
  await click(board + '/history/redo');
  check(await until(`${name(board + '/items/task-1/done')}==='Review keyboard and focus'`), 'the actual redo control preserves and replays the edit');
  await b.call('Browser.grantPermissions',{origin:server.url.slice(0,-1),permissions:['clipboardReadWrite','clipboardSanitizedWrite']});
  await click(board + '/history/copy');
  check(await until(`navigator.clipboard.readText().then(t=>t.includes('- [ ] Review keyboard and focus\\n'))`), 'the permitted clipboard receives a usable Markdown checklist');
  await click(board + '/items/task-1/done');
  check(await until(`${name(motion + '/shape/task-1/state')}==='Complete'`), 'a pointer completion updates the other view');
  // Entering this section can itself seek the layout. Sample its chosen
  // target only after scrolling, before testing the manual override.
  await intoView(motion + '/controls/change'); await sleep(300);
  const before = await b.value(`window.omui.snapshot({root:${JSON.stringify(motion + '/shape')}}).then(r=>r.data.nodes.map(n=>n.id).sort())`);
  const mode = await b.value(name(motion + '/controls/change'));
  await click(motion + '/controls/change');
  check(await until(`${name(motion + '/controls/change')}!==${JSON.stringify(mode)}`), 'a click changes layout without requiring a scroll gesture');
  const after = await b.value(`window.omui.snapshot({root:${JSON.stringify(motion + '/shape')}}).then(r=>r.data.nodes.map(n=>n.id).sort())`);
  check(JSON.stringify(before) === JSON.stringify(after), 'layout changes keep all task node ids');
  check(await b.value(name(motion + '/shape/task-1/title')) === 'Review keyboard and focus', 'the edited title survives the layout change');
  await shot('motion-en-desktop');
  await act('main/bar/nav/to_docs','activate');
  await until(node('main/body/view/docs/page'));
  // The brand returns to the landing. The release workspace is a separate
  // working tool under Creative demos; open it without reloading the runtime.
  await act('main/bar/nav/to_explore','activate');
  const release = 'main/body/view/explore/page/explore/works/release/open';
  const toolReady = await until(node(release));
  check(toolReady, 'Creative demos exposes the release workspace');
  if (!toolReady) throw new Error('The release workspace destination did not appear');
  await click(release, 'main/body/view/explore');
  const restored = await until(`location.hash==='#/scenes/release' && ${name(board + '/items/task-1/done')}==='Review keyboard and focus'`);
  check(restored, 'visiting documentation preserves the workspace data');
  if (!restored) throw new Error('Returning to the release route did not restore the edited workspace');
  await b.call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
  await act('main/bar/lang','activate');
  check(await until(`${name(board + '/items/docs/done')}==='文档已审阅'`), 'Chinese translates the seeded tasks');
  check(await b.value(name(board + '/items/task-1/done')) === 'Review keyboard and focus', 'changing language preserves user text');
  await act('main/body/view/release','scroll_to',{x:0,y:0});
  await shot('home-zh-phone');
  const layout = await b.value(`window.omui.snapshot({root:${JSON.stringify(board)}}).then(r=>r.data.nodes.filter(n=>n.role==='button'&&n.bounds&&!n.state?.includes('hidden')&&!n.state?.includes('offscreen')).map(n=>({id:n.id,b:n.bounds})))`);
  if (!Array.isArray(layout)) throw new Error('The phone workspace snapshot was unavailable: ' + JSON.stringify(layout));
  check(layout?.length>3 && layout.every(n=>n.b[0]>=-0.5&&n.b[0]+n.b[2]<=390.5), 'phone workspace controls fit the viewport');
  const short = layout.filter(n=>n.id.endsWith('/edit')||n.id.endsWith('/add/submit'));
  check(short.length>0 && short.every(n=>n.b[3]<45), 'short Chinese action labels stay on one line');
  await click(board + '/items/task-1/edit');
  await click(board + '/editor/actions/remove');
  check(await until(`!${node(board + '/items/task-1/done')} && !${node(motion + '/shape/task-1/title')}`), 'removing a task removes both views');
  await click(board + '/history/undo');
  check(await until(`${name(board + '/items/task-1/done')}==='Review keyboard and focus'`), 'undo restores the removed task with the same id');
  await click(board + '/actions/reset');
  check(await until(`!${node(board + '/items/task-1/done')}`), 'reset returns to the seed board');
  await click(board + '/history/undo');
  check(await until(`${name(board + '/items/task-1/done')}==='Review keyboard and focus'`), 'reset itself is reversible');
} catch(error) { check(false,'browser workspace scenario completed',String(error.stack||error)); }
finally { b.close(); server.close(); }
report.ok=report.checks.every(c=>c.ok);
console.log(JSON.stringify(report,null,2)); process.exit(report.ok?0:1);
