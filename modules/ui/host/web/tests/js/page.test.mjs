// The page's half of openmui.host.web (src/js/omui_web.js) under node, with
// a fake DOM (fake_dom.mjs): the library is loaded as Emscripten would link
// it, its helpers stood in for (a "pointer" is the string itself, the
// program's callback a function that records what it is given). What the C++
// side hears and what the page ends up holding are checked, deterministically.
//
//   node --test modules/ui/host/web/tests/js/page.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { Event, Storage, Window } from './fake_dom.mjs';

// Values made in the library's realm, compared as data.
const plain = (v) => JSON.parse(JSON.stringify(v));
const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const library = readFileSync(here('../../src/js/omui_web.js'), 'utf8');

// A page with a canvas at (100, 50), 200×160 CSS pixels, and the library
// opened on it: what the program was told, in order, in `heard`.
function page({ storage = new Storage(), clipboard = null, permissions = null, activation = undefined, fetch = undefined } = {}) {
  const window = new Window();
  const document = window.document;
  const canvas = document.createElement('canvas');
  canvas.setAttribute('id', 'canvas');
  canvas.box = { left: 100, top: 50, width: 200, height: 160 };
  document.body.appendChild(canvas);
  const written = [];
  const navigator = {
    languages: ['zh-CN', 'en'],
    clipboard: clipboard ?? {
      writeText: (t) => { written.push(t); return Promise.resolve(); },
      readText: () => Promise.resolve('from the system'),
    },
    permissions: permissions ?? { query: () => Promise.resolve({ state: 'granted' }) },
    userActivation: activation,
  };
  const context = {
    window, document, navigator, Module: {}, console,
    HEAPF32: new Float32Array(8),
    UTF8ToString: (s) => s,
    stringToNewUTF8: (s) => s,
    lengthBytesUTF8: (s) => Buffer.byteLength(s, 'utf8'),
    getWasmTableEntry: (f) => f,
    fetch, setTimeout, crypto: globalThis.crypto, location: { href: 'https://example.org/app' },
  };
  Object.defineProperty(context, 'localStorage', {
    get() {
      if (storage === 'refused') throw Object.assign(new Error('The operation is insecure.'), { name: 'SecurityError' });
      return storage;
    },
  });
  let lib = null;
  context.addToLibrary = (o) => { lib = o; };
  vm.createContext(context);
  vm.runInContext(library, context, { filename: 'omui_web.js' });
  for (const [k, v] of Object.entries(lib)) {
    if (k.endsWith('__deps')) continue;
    context[k.startsWith('$') ? k.slice(1) : k] = v;
  }
  const heard = [];
  context.omui_web_open('#canvas', (kind, a, b, text) => heard.push([kind, a, b, text]));
  const P = context.omuiPage;
  const fire = (target, type, fields = {}, trusted = true) => document.dispatch(target, new Event(type, fields, trusted));
  return { window, document, canvas, context, P, heard, fire, written, storage };
}

test('the C++ side and the page agree on what each kind of event is', () => {
  const cpp = readFileSync(here('../../src/native/browser/html5.cpp'), 'utf8');
  const { P } = page();
  const m = /page_text = (\d+), page_preedit = (\d+), page_key = (\d+), page_activate = (\d+), page_focus = (\d+), page_agent = (\d+), page_link = (\d+)/.exec(cpp);
  assert.ok(m, 'html5.cpp names the kinds');
  assert.deepEqual(m.slice(1).map(Number), [P.TEXT, P.PREEDIT, P.KEY, P.ACTIVATE, P.FOCUS, P.AGENT, P.LINK]);
});

test('opening: the textarea, the mirror\'s layer, the live regions, window.omui', () => {
  const { document, window } = page();
  for (const what of ['input', 'mirror', 'live', 'alert']) assert.ok(document.querySelector(`[data-omui="${what}"]`), what);
  assert.equal(document.querySelector('[data-omui="live"]').getAttribute('aria-live'), 'polite');
  assert.equal(document.querySelector('[data-omui="alert"]').getAttribute('aria-live'), 'assertive');
  const t = document.querySelector('[data-omui="input"]');
  assert.equal(t.tagName, 'TEXTAREA');
  assert.equal(t.style.opacity, '0', 'out of sight, not display:none (still read)');
  assert.ok(Object.isFrozen(window.omui));
  for (const op of ['request', 'describe', 'snapshot', 'changes', 'find', 'act', 'invoke', 'wait', 'capture'])
    assert.equal(typeof window.omui[op], 'function', op);
});

test('only the person\'s own input is input: a script\'s events are marked untrusted', () => {
  const { window, canvas, context, fire } = page();
  fire(canvas, 'mousedown', {}, true);
  assert.equal(context.omui_web_trusted(), 1);
  fire(canvas, 'mousedown', {}, false);
  assert.equal(context.omui_web_trusted(), 0, 'dispatched by the page\'s script');
  window.dispatchEvent(new Event('keydown', { code: 'KeyA' }, false));
  assert.equal(context.omui_web_trusted(), 0, 'at the window itself');
  fire(canvas, 'keydown', { code: 'KeyA' }, true);
  assert.equal(context.omui_web_trusted(), 1);
});

test('an input method composes in the textarea: preedit as it goes, then the text', () => {
  const { P, heard, fire } = page();
  const t = P.textarea;
  fire(t, 'compositionstart', { data: '' });
  t.value = 'ni';
  fire(t, 'compositionupdate', { data: 'ni' });
  t.value = '你好';
  fire(t, 'compositionupdate', { data: '你好' });
  fire(t, 'input', { inputType: 'insertCompositionText', data: '你好', isComposing: true });
  fire(t, 'compositionend', { data: '你好' });
  assert.deepEqual(heard, [
    [P.PREEDIT, 2, 2, 'ni'],
    [P.PREEDIT, 6, 6, '你好'],     // bytes: openmui counts bytes
    [P.PREEDIT, -1, -1, ''],      // the composition over
    [P.TEXT, 0, 0, '你好'],
  ]);
  assert.equal(t.value, '', 'the textarea is emptied for the next');
  heard.length = 0;
  fire(t, 'compositionstart', { data: '' }, false);
  fire(t, 'compositionupdate', { data: 'x' }, false);
  fire(t, 'compositionend', { data: 'x' }, false);
  assert.deepEqual(heard, [], 'a script\'s composition is not the person\'s');
});

test('a phone\'s keyboard: letters and edits as input events', () => {
  const { P, heard, fire } = page();
  const t = P.textarea;
  fire(t, 'input', { inputType: 'insertText', data: 'a' });
  fire(t, 'input', { inputType: 'deleteContentBackward' });
  fire(t, 'input', { inputType: 'insertLineBreak' });
  fire(t, 'input', { inputType: 'insertText', data: 'b' }, false);
  assert.deepEqual(heard, [[P.TEXT, 0, 0, 'a'], [P.KEY, 0, 0, 'Backspace'], [P.KEY, 0, 0, 'Enter']]);
});

test('the textarea goes to the caret, named for the field, and away again', () => {
  const { P, document, context } = page();
  context.omui_web_text_input(10, 20, 1.5, 18, 'Title');
  const t = P.textarea;
  assert.equal(document.activeElement, t);
  assert.equal(t.style.left, '110px');
  assert.equal(t.style.top, '70px');
  assert.equal(t.style.height, '18px');
  assert.equal(t.getAttribute('aria-label'), 'Title');
  context.omui_web_text_input(0, 0, -1, -1, '');
  assert.equal(document.activeElement, document.body, 'put away');
  // The focus elsewhere on the page (a site's own field) is not taken.
  const other = document.createElement('input');
  document.body.appendChild(other);
  other.focus();
  context.omui_web_text_input(10, 20, 1.5, 18, 'Title');
  assert.equal(document.activeElement, other);
});

test('the clipboard: a paste by the chord is the program\'s key; otherwise it is text', async () => {
  const { P, heard, fire, context, written, window } = page();
  const t = P.textarea;
  t.focus();
  const data = (text) => ({ clipboardData: { getData: (type) => (type === 'text/plain' ? text : '') } });
  fire(t, 'keydown', { code: 'KeyV', ctrlKey: true });
  const chord = fire(t, 'paste', data('pasted by keys'));
  assert.ok(chord.defaultPrevented, 'nothing lands in the textarea');
  assert.equal(context.omui_web_clipboard(), 'pasted by keys');
  assert.deepEqual(heard, [], 'the program reads it when it hears Ctrl+V');
  fire(t, 'keydown', { code: 'KeyA' });
  fire(t, 'paste', data('from a menu'));
  assert.deepEqual(heard, [[P.TEXT, 0, 0, 'from a menu']]);
  context.omui_web_set_clipboard('copied');
  assert.equal(context.omui_web_clipboard(), 'copied');
  assert.deepEqual(written, ['copied'], 'through the Async Clipboard API');
  // Back in the page, with reading allowed: what the system holds now.
  window.dispatchEvent(new Event('focus'));
  await new Promise((r) => setTimeout(r, 0));
  assert.equal(context.omui_web_clipboard(), 'from the system');
});

// What DomMirror sends (the C++ test pins its form): made, then changed.
const first = JSON.stringify({
  root: 'app',
  nodes: [
    { id: 'app', children: ['main/title', 'main/save', 'main/status'], box: [0, 0, 200, 160] },
    { id: 'main/title', set: { role: 'none' }, text: 'Notes', box: [10, 10, 180, 22] },
    { id: 'main/save', set: { 'aria-label': 'Save', role: 'button', tabindex: '-1' }, box: [10, 60, 60, 24] },
    { id: 'main/status', set: { 'aria-label': 'Ready', 'aria-live': 'off', role: 'status' }, box: [10, 88, 100, 16] },
  ],
  revision: 17,
});

test('an address: a new tab inside the person\'s activation, else this tab; never a script', () => {
  const p = page({ activation: { isActive: true } });
  assert.equal(p.context.omui_web_open_url('https://github.com/openlifa/openmui'), 1);
  assert.deepEqual(plain(p.window.opened), [['https://github.com/openlifa/openmui', '_blank', 'noopener,noreferrer']]);
  assert.equal(p.context.omui_web_open_url('javascript:alert(1)'), 0, 'not an address');
  assert.equal(p.context.omui_web_open_url('data:text/html,hi'), 0);
  const late = page({ activation: { isActive: false } });
  assert.equal(late.context.omui_web_open_url('mailto:speakshen@outlook.com'), 1);
  assert.deepEqual(plain(late.window.opened), [], 'no window without the person\'s activation');
  assert.deepEqual(plain(late.window.assigned), ['mailto:speakshen@outlook.com']);
});

test('the semantic DOM mirror: elements made, then only what changed', () => {
  const { P, document, context, heard } = page();
  context.omui_web_mirror(first);
  const layer = document.querySelector('[data-omui="mirror"]');
  assert.equal(layer.style.left, '100px', 'over the canvas');
  assert.equal(layer.style.width, '200px');
  const app = layer.firstElementChild;
  assert.equal(app.getAttribute('data-omui-id'), 'app');
  assert.deepEqual(app.children.map((c) => c.getAttribute('data-omui-id')), ['main/title', 'main/save', 'main/status']);
  const save = P.nodes.get('main/save');
  assert.equal(save.getAttribute('role'), 'button');
  assert.equal(save.getAttribute('aria-label'), 'Save');
  assert.equal(save.style.left, '10px');
  assert.equal(save.style.width, '60px');
  assert.equal(P.nodes.get('main/title').textContent, 'Notes');
  // Changed: one attribute gone, a text, an order, a node removed, the focus, the live region.
  context.omui_web_mirror(JSON.stringify({
    remove: ['main/status'],
    nodes: [
      { id: 'main/save', set: { 'aria-label': 'Save all' }, unset: ['tabindex'] },
      { id: 'main/title', text: 'Notes (3)' },
      { id: 'main/toast', set: { role: 'none' }, text: 'Saved', box: [10, 120, 80, 20] },
      { id: 'app', children: ['main/save', 'main/title', 'main/toast'] },
    ],
    focus: 'main/save',
    live: 'Saved 3 notes',
  }));
  assert.deepEqual(app.children.map((c) => c.getAttribute('data-omui-id')), ['main/save', 'main/title', 'main/toast']);
  assert.equal(save.getAttribute('aria-label'), 'Save all');
  assert.equal(save.hasAttribute('tabindex'), false);
  assert.equal(P.nodes.get('main/title').textContent, 'Notes (3)');
  assert.equal(P.nodes.has('main/status'), false);
  assert.equal(document.activeElement, save, 'the focus where the program has it');
  assert.deepEqual(heard, [], 'the program\'s own focus move is not reported back');
  assert.equal(document.querySelector('[data-omui="live"]').textContent, 'Saved 3 notes');
  context.omui_web_mirror(JSON.stringify({ focus: null, alert: 'Delete everything?' }));
  assert.equal(document.querySelector('[data-omui="alert"]').textContent, 'Delete everything?');
});

test('assistive technology in the mirror: a click activates, a focus moves the program\'s', () => {
  const { P, context, heard, fire, document } = page();
  context.omui_web_mirror(first);
  const save = P.nodes.get('main/save');
  fire(save, 'click');
  save.click();  // a script's: not the person's
  const title = P.nodes.get('main/title');
  document.activeElement = document.body;
  fire(title, 'focus');
  assert.deepEqual(heard, [[P.ACTIVATE, 0, 0, 'main/save'], [P.FOCUS, 0, 0, 'main/title']]);
});

test('the skip link: the first Tab stop, seen while focused, the focus taken to the main landmark', () => {
  const { P, context, heard, fire, document, canvas } = page();
  const skip = document.querySelector('[data-omui="skip"]');
  assert.ok(skip, 'made');
  assert.equal(document.body.firstChild, skip, 'before the canvas: the first stop');
  assert.equal(skip.hidden, true, 'hidden while the page has no main landmark to go to');
  context.omui_web_mirror(JSON.stringify({ skip: { to: 'main/view/more', name: 'The place' } }));
  assert.equal(skip.hidden, false);
  assert.equal(skip.textContent, 'Skip to main content');
  skip.focus();
  assert.equal(skip.style.top, '8px', 'seen once focused');
  const click = new Event('click', {}, true);
  document.dispatch(skip, click);
  assert.ok(click.defaultPrevented, 'no navigation (the hash is the program\'s route)');
  assert.equal(document.activeElement, canvas, 'the page\'s focus on the canvas, the program\'s to move');
  assert.deepEqual(heard, [[P.FOCUS, 0, 0, 'main/view/more']]);
  fire(skip, 'click', {}, false);  // a script's: not the person's
  assert.equal(heard.length, 1);
  context.omui_web_mirror(JSON.stringify({ skip: null }));
  assert.equal(skip.hidden, true, 'nothing to go to: gone');
});

test('the skip link by the keys: the first Tab while the program has no focus, Enter follows it, Tab goes on into the program', () => {
  const { P, context, heard, document, window, canvas } = page();
  const program = [];  // the program's keys, heard on the window after the page's (Emscripten's listener)
  window.addEventListener('keydown', (e) => { program.push(e.key); e.preventDefault(); }, true);
  const skip = document.querySelector('[data-omui="skip"]');
  const press = (target, k, fields = {}) => document.dispatch(target, new Event('keydown', { key: k, ...fields }, true));
  press(canvas, 'Tab');
  assert.deepEqual(program, ['Tab'], 'no skip link yet: the program\'s');
  context.omui_web_mirror(JSON.stringify({ skip: { to: 'main/view/more', name: 'The place' } }));
  canvas.focus();
  const tab = press(canvas, 'Tab');
  assert.equal(document.activeElement, skip, 'the first Tab: the skip link');
  assert.ok(tab.defaultPrevented);
  assert.deepEqual(program, ['Tab'], 'not the program\'s');
  press(skip, 'Enter');
  assert.deepEqual(heard, [[P.FOCUS, 0, 0, 'main/view/more']], 'Enter follows it');
  assert.deepEqual(program, ['Tab'], 'nor its Enter');
  assert.equal(document.activeElement, canvas);
  // Once the program has a focus, Tab is its own.
  context.omui_web_mirror(JSON.stringify({ focus: 'main/view/more' }));
  press(canvas, 'Tab');
  assert.deepEqual(program, ['Tab', 'Tab']);
  // From the link, Tab goes on into the program, which takes it.
  P.focused = null;
  skip.focus();
  press(skip, 'Tab');
  assert.equal(document.activeElement, canvas);
  assert.deepEqual(program, ['Tab', 'Tab', 'Tab']);
});

test('window.omui: requests go to the program, its records settle the promise', async () => {
  const { P, window, context, heard } = page();
  const found = window.omui.find({ role: 'button', name: 'Save*' });
  const acted = window.omui.act('main/save', 'activate', { answer: 'yes', actor: 'agent:helper' });
  assert.equal(heard.length, 2);
  const [kind, , , line] = heard[0];
  assert.equal(kind, P.AGENT);
  assert.deepEqual(JSON.parse(line), { op: 'find', q: { role: 'button', name: 'Save*' }, re: 1 });
  assert.deepEqual(JSON.parse(heard[1][3]), { op: 'act', node: 'main/save', action: 'activate', answer: 'yes', actor: 'agent:helper', re: 2 });
  // Answered out of order, as a wait would be.
  context.omui_web_agent_reply(JSON.stringify({ kind: 'error', code: 'needs-answer', text: '…', re: 2 }));
  context.omui_web_agent_reply(JSON.stringify({ kind: 'result', exitCode: 2, re: 2 }));
  context.omui_web_agent_reply(JSON.stringify({ kind: 'data', data: { nodes: [{ id: 'main/save' }] }, re: 1 }));
  context.omui_web_agent_reply(JSON.stringify({ kind: 'result', exitCode: 0, re: 1 }));
  context.omui_web_agent_reply(JSON.stringify({ kind: 'result', exitCode: 0, re: 99 }));  // no one's: dropped
  const f = await found, a = await acted;
  assert.equal(f.exitCode, 0);
  assert.deepEqual(plain(f.data), { nodes: [{ id: 'main/save' }] });
  assert.equal(a.exitCode, 2);
  assert.equal(a.errors[0].code, 'needs-answer');
  assert.equal(a.records.length, 2);
  assert.equal(P.agents.size, 0);
});

// A bridge (omui connect) as the page meets it: hello with the code, the next request, the reply.
function bridge(code, requests) {
  const seen = { hellos: [], replies: [] };
  const answer = (status, body) => Promise.resolve({ ok: status === 200, status, json: () => Promise.resolve(body) });
  const fetch = (url, init = {}) => {
    const u = new URL(url);
    if (u.port !== '47111') return Promise.reject(new TypeError('connection refused'));  // the first free port was 47111
    if (u.pathname === '/omui/v1/hello') {
      const hello = JSON.parse(init.body);
      seen.hellos.push(hello);
      return hello.code.replace('-', '') === code.replace('-', '') ? answer(200, { session: 's1', agent: 'Claude Code', bridge: '0.1.0' })
                                                                     : answer(403, { error: 'not the pairing code' });
    }
    if (u.searchParams.get('session') !== 's1') return answer(403, { error: 'not this session' });
    if (u.pathname === '/omui/v1/next') {
      const job = requests.shift();
      return job ? answer(200, job) : new Promise(() => {});  // nothing more: it waits
    }
    if (u.pathname === '/omui/v1/reply') {
      seen.replies.push(JSON.parse(init.body));
      return answer(200, {});
    }
    return answer(404, {});
  };
  return { fetch, seen };
}

test('the agent link: a code made in the page, a bridge found at its port, the agent\'s requests answered through window.omui\'s path', async () => {
  // The bridge is started with the code the page made (the person gave it to their agent): made once the code is known.
  let b = null;
  const { P, window, context, heard } = page({ fetch: (u, i) => (b ? b.fetch(u, i) : Promise.reject(new TypeError('nothing listens yet'))) });
  const code = context.omui_web_link_start('org.example.notes');
  assert.match(code, /^[A-HJ-NP-Z2-9]{4}-[A-HJ-NP-Z2-9]{4}$/, 'eight letters and digits, none easily misread');
  assert.equal(window.omui.link(), code, 'asked again while it looks: the same code');
  const said = () => heard.filter((h) => h[0] === P.LINK).map((h) => JSON.parse(h[3]));
  assert.equal(said()[0].state, 'waiting');
  b = bridge(code, [{ id: 1, request: { op: 'snapshot', actor: 'agent:Claude Code' } }]);
  await new Promise((r) => setTimeout(r, 1700));  // the page looks again every 1.5 s
  const paired = said().find((x) => x.state === 'paired');
  assert.ok(paired, 'paired');
  assert.equal(paired.agent, 'Claude Code');
  assert.equal(paired.port, 47111);
  assert.equal(b.seen.hellos[0].app, 'org.example.notes');
  // Its request reached the program as window.omui's do; the program's records went back as the reply.
  await new Promise((r) => setTimeout(r, 20));
  const asked = heard.filter((h) => h[0] === P.AGENT);
  assert.equal(asked.length, 1);
  assert.deepEqual(JSON.parse(asked[0][3]), { op: 'snapshot', actor: 'agent:Claude Code', re: 1 });
  context.omui_web_agent_reply(JSON.stringify({ kind: 'data', data: { nodes: [] }, re: 1 }));
  context.omui_web_agent_reply(JSON.stringify({ kind: 'result', exitCode: 0, re: 1 }));
  await new Promise((r) => setTimeout(r, 20));
  assert.equal(b.seen.replies.length, 1);
  assert.equal(b.seen.replies[0].id, 1);
  assert.equal(b.seen.replies[0].reply.exitCode, 0);
  context.omui_web_link_stop();
  assert.equal(said().at(-1).state, 'stopped');
});

test('the agent link refuses nothing itself: a wrong code is the bridge\'s to refuse, and the page keeps looking', async () => {
  const b = bridge('WXYZ-2345', []);
  const { P, context, heard } = page({ fetch: b.fetch });
  context.omui_web_link_start('app');
  await new Promise((r) => setTimeout(r, 50));
  const said = heard.filter((h) => h[0] === P.LINK).map((h) => JSON.parse(h[3]));
  assert.equal(b.seen.hellos.length, 1);
  assert.equal(said.at(-1).state, 'waiting');
  assert.match(said.at(-1).error, /pairing code/);
  context.omui_web_link_stop();
});

test('localStorage: kept, listed by prefix, full, refused', () => {
  const storage = new Storage(64);
  const { context } = page({ storage });
  assert.equal(context.omui_web_store(), 1);
  assert.equal(context.omui_web_store_set('omui/app/best', '1200'), 1);
  assert.equal(context.omui_web_store_set('omui/app/name', 'Kun'), 1);
  assert.equal(context.omui_web_store_set('omui/other/x', '1'), 1);
  assert.equal(context.omui_web_store_get('omui/app/best'), '1200');
  assert.equal(context.omui_web_store_get('omui/app/missing'), 0, 'none: a null pointer');
  assert.deepEqual(JSON.parse(context.omui_web_store_keys('omui/app/')), ['omui/app/best', 'omui/app/name']);
  assert.equal(context.omui_web_store_set('omui/app/big', 'x'.repeat(64)), 0, 'full');
  assert.equal(context.omui_web_store_remove('omui/app/name'), 1);
  assert.equal(context.omui_web_store_remove('omui/app/name'), 0);
  const refused = page({ storage: 'refused' });
  assert.equal(refused.context.omui_web_store(), 0, 'turned off by the person');
  assert.equal(refused.context.omui_web_store_set('k', 'v'), 0);
});
