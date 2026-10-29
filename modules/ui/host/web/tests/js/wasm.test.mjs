// The browser host end to end: tests/page_test.cpp, built for
// wasm32-emscripten (tools/ci/web.sh builds it), run in this process in a
// fake page (fake_dom.mjs) whose frames this test steps by hand — so the
// program's C++, the library's JavaScript and the boundary between them
// are checked together, deterministically: the person composing in the
// hidden textarea, typing, pasting; a screen reader activating in the
// mirror; the page's agent through window.omui, decided as any agent's.
//
//   OMUI_PAGE_TEST=target/wasm32-emscripten/<hash>/bin/web/page_test.js \
//     node --test modules/ui/host/web/tests/js/wasm.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readdirSync, statSync } from 'node:fs';
import { resolve } from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { Event, Storage, Window } from './fake_dom.mjs';

const root = fileURLToPath(new URL('../../../../../../', import.meta.url));

// The program as built: named by the Web row, else the newest build here.
function built() {
  if (process.env.OMUI_PAGE_TEST) return resolve(process.env.OMUI_PAGE_TEST);
  const dir = root + 'target/wasm32-emscripten';
  if (!existsSync(dir)) return null;
  const found = readdirSync(dir)
    .map((h) => `${dir}/${h}/bin/web/page_test.js`)
    .filter((p) => existsSync(p))
    .sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs);
  return found[0] ?? null;
}

const program = built();

test('the browser host end to end, in a page', { skip: !program && 'page_test.js is not built (tools/ci/web.sh builds it)' }, async () => {
  assert.ok(existsSync(program), `${program} exists`);
  const window = new Window();
  const document = window.document;
  const canvas = document.createElement('canvas');
  canvas.setAttribute('id', 'canvas');
  canvas.box = { left: 20, top: 30, width: 320, height: 200 };
  const shown = [];
  canvas.getContext = () => ({ putImageData: (image) => shown.push([image.width, image.height]) });
  document.body.appendChild(canvas);
  const storage = new Storage();
  const written = [];
  const frames = [];
  const define = (k, v) => Object.defineProperty(globalThis, k, { value: v, configurable: true, writable: true });
  // The page first (Emscripten notes the document and the window as it
  // loads), then the program, loaded as node loads it (node it is: its
  // files are read from disk).
  define('window', window);
  define('document', document);
  define('navigator', {
    languages: ['en'],
    getGamepads: () => [],
    clipboard: { writeText: (t) => { written.push(t); return Promise.resolve(); } },
  });
  define('localStorage', storage);
  define('requestAnimationFrame', (f) => frames.push(f));
  define('ImageData', class { constructor(data, width, height) { Object.assign(this, { data, width, height }); } });
  createRequire(import.meta.url)(program);

  const settle = () => new Promise((r) => setImmediate(r));
  for (let i = 0; i < 2000 && !(window.omui && frames.length > 0); ++i) await new Promise((r) => setTimeout(r, 5));
  assert.ok(window.omui && frames.length > 0, 'the program opened the page and handed it the loop');
  let now = 0;
  const tick = async (n = 1) => {
    for (let i = 0; i < n; ++i) {
      now += 1000 / 60;
      for (const f of frames.splice(0)) f(now);
      await settle();
    }
  };
  const fire = (target, type, fields = {}, trusted = true) => document.dispatch(target, new Event(type, fields, trusted));
  const node = (id) => document.querySelector(`[data-omui-id="${id}"]`);

  await tick();
  // Drawn, mirrored, the textarea at the caret.
  assert.ok(shown.length > 0, 'a frame on the canvas');
  assert.equal(document.title, 'openmui page test');
  const note = node('main/note');
  assert.ok(note, 'the mirror made the field');
  assert.equal(note.getAttribute('role'), 'textbox');
  assert.equal(note.getAttribute('aria-label'), 'Note: []');
  assert.equal(node('main/press').getAttribute('role'), 'button');
  assert.equal(node('main/status').getAttribute('aria-label'), 'Pressed 0');
  const t = document.querySelector('[data-omui="input"]');
  assert.equal(document.activeElement, t, 'the textarea has the focus while the field is edited');
  assert.equal(t.getAttribute('aria-label'), 'Note: []', 'named for the field');
  assert.equal(parseFloat(t.style.left), 20 + Number.parseFloat(note.style.left) + 4, 'at the caret');
  assert.equal(storage.getItem('omui/org.openmui.page-test/visits'), '1', 'the store is localStorage, under the app');

  // An input method composes, then commits.
  fire(t, 'compositionstart', { data: '' });
  fire(t, 'compositionupdate', { data: 'ni' });
  await tick();
  assert.equal(note.getAttribute('aria-label'), 'Note: [ni]', 'the preedit reached the field');
  fire(t, 'compositionupdate', { data: '你' });
  fire(t, 'compositionend', { data: '你' });
  await tick();
  assert.equal(note.getAttribute('aria-label'), 'Note: 你[]');

  // Keys: the person's are typed; a script's are not.
  const key = (k, code, trusted) => fire(t, 'keydown', { key: k, code, keyCode: k.toUpperCase().charCodeAt(0), which: 0, charCode: 0, location: 0 }, trusted);
  key('a', 'KeyA', true);
  key('b', 'KeyB', false);
  await tick();
  assert.equal(note.getAttribute('aria-label'), 'Note: 你a[]');
  // A paste that is not the keys' chord (a phone's menu) is text.
  fire(t, 'paste', { clipboardData: { getData: () => '!' } });
  await tick();
  assert.equal(note.getAttribute('aria-label'), 'Note: 你a![]');

  // A screen reader activates the button in the mirror: a press, and the status said.
  fire(node('main/press'), 'click');
  node('main/press').click();  // a script's click: nothing
  await tick();
  assert.equal(node('main/status').getAttribute('aria-label'), 'Pressed 1');
  assert.equal(document.querySelector('[data-omui="live"]').textContent, 'Pressed 1');

  // The page's agent: finds, acts, and is refused the person's own.
  const found = window.omui.find({ role: 'button' });
  const pressed = window.omui.act('main/press', 'activate');
  const refused = window.omui.act('main/allow', 'activate', { actor: 'person:user' });
  await tick(2);
  const f = await found, p = await pressed, r = await refused;
  assert.equal(f.exitCode, 0);
  assert.deepEqual(f.data.nodes.map((n) => n.id).sort(), ['main/allow', 'main/press']);
  assert.equal(p.exitCode, 0);
  assert.equal(r.exitCode, 1, 'the person\'s own, over the bus: refused');
  assert.equal(r.errors[0].code, 'refused');
  await tick();
  assert.equal(node('main/status').getAttribute('aria-label'), 'Pressed 2', 'the agent\'s press, not the refused one');
  const waited = window.omui.wait({ q: { name: 'Pressed 2' } }, { timeout: 1 });
  await tick();
  assert.equal((await waited).exitCode, 0);
});
