// A small DOM, enough for the page's half of openmui.host.web
// (src/js/omui_web.js): elements with attributes, styles, children and
// listeners; text nodes; focus; events dispatched through the capture and
// bubble phases, trusted or not, as a browser would. Not a browser: no
// layout (the canvas states its box), no CSS, no parsing.

// What bubbles (focus, blur, mouseenter and mouseleave do not).
const bubbles = new Set(['click', 'keydown', 'keyup', 'mousedown', 'mouseup', 'mousemove', 'wheel', 'touchstart', 'touchend',
  'touchmove', 'touchcancel', 'input', 'paste', 'copy', 'cut', 'compositionstart', 'compositionupdate', 'compositionend']);

export class Event {
  constructor(type, fields = {}, trusted = true) {
    this.type = type;
    this.isTrusted = trusted;
    this.defaultPrevented = false;
    this.stopped = false;
    Object.assign(this, fields);
  }
  preventDefault() { this.defaultPrevented = true; }
  stopPropagation() { this.stopped = true; }
  stopImmediatePropagation() { this.stopped = true; }
}

class Target {
  constructor() { this.listeners = []; }
  addEventListener(type, fn, capture) {
    this.listeners.push({ type, fn, capture: capture === true || (capture && capture.capture === true) });
  }
  removeEventListener(type, fn) { this.listeners = this.listeners.filter((l) => l.type !== type || l.fn !== fn); }
  fire(event, phase) {
    for (const l of this.listeners.slice()) {
      if (l.type !== event.type) continue;
      if (phase === 'capture' && !l.capture) continue;
      if (phase === 'bubble' && l.capture) continue;
      event.currentTarget = this;
      l.fn.call(this, event);
      if (event.stopped) return;
    }
  }
}

export class Text {
  constructor(data) {
    this.nodeType = 3;
    this.data = data;
    this.parentNode = null;
  }
  get textContent() { return this.data; }
}

export class Element extends Target {
  constructor(document, tag) {
    super();
    this.ownerDocument = document;
    this.nodeType = 1;
    this.tagName = tag.toUpperCase();
    this.attributes = new Map();
    this.style = {};
    this.childNodes = [];
    this.parentNode = null;
    this.value = '';
    this.selectionStart = 0;
    this.selectionEnd = 0;
    this.box = { left: 0, top: 0, width: 0, height: 0 };
  }
  setAttribute(k, v) { this.attributes.set(k, String(v)); }
  getAttribute(k) { return this.attributes.has(k) ? this.attributes.get(k) : null; }
  hasAttribute(k) { return this.attributes.has(k); }
  removeAttribute(k) { this.attributes.delete(k); }
  get id() { return this.getAttribute('id') || ''; }
  get firstChild() { return this.childNodes[0] || null; }
  get children() { return this.childNodes.filter((c) => c.nodeType === 1); }
  get firstElementChild() { return this.children[0] || null; }
  get nextElementSibling() {
    if (!this.parentNode) return null;
    const siblings = this.parentNode.childNodes;
    for (let i = siblings.indexOf(this) + 1; i < siblings.length; ++i) if (siblings[i].nodeType === 1) return siblings[i];
    return null;
  }
  get nextSibling() {
    if (!this.parentNode) return null;
    const siblings = this.parentNode.childNodes;
    return siblings[siblings.indexOf(this) + 1] || null;
  }
  detach(node) {
    if (node.parentNode) {
      const list = node.parentNode.childNodes;
      list.splice(list.indexOf(node), 1);
      node.parentNode = null;
    }
  }
  appendChild(node) { return this.insertBefore(node, null); }
  insertBefore(node, before) {
    this.detach(node);
    const at = before ? this.childNodes.indexOf(before) : -1;
    if (at < 0) this.childNodes.push(node);
    else this.childNodes.splice(at, 0, node);
    node.parentNode = this;
    return node;
  }
  removeChild(node) {
    if (node.parentNode !== this) throw new Error('not a child');
    this.detach(node);
    return node;
  }
  contains(node) {
    for (let n = node; n; n = n.parentNode) if (n === this) return true;
    return false;
  }
  get textContent() { return this.childNodes.map((c) => c.textContent).join(''); }
  set textContent(t) {
    for (const c of this.childNodes) c.parentNode = null;
    this.childNodes = t ? [new Text(t)] : [];
    for (const c of this.childNodes) c.parentNode = this;
  }
  getBoundingClientRect() { return { ...this.box }; }
  // As a browser does: focus() fires a trusted focus event (the browser's,
  // though a script asked); blur() a trusted blur.
  focus() {
    const d = this.ownerDocument;
    if (d.activeElement === this) return;
    const before = d.activeElement;
    d.activeElement = this;
    if (before && before !== d.body) d.dispatch(before, new Event('blur', {}, true));
    d.dispatch(this, new Event('focus', {}, true));
  }
  blur() {
    const d = this.ownerDocument;
    if (d.activeElement !== this) return;
    d.activeElement = d.body;
    d.dispatch(this, new Event('blur', {}, true));
  }
  // A script's click: not the person's.
  click() { this.ownerDocument.dispatch(this, new Event('click', {}, false)); }
  // Selector matching for what the library asks: #id, or [attr="value"].
  matches(selector) {
    if (selector.startsWith('#')) return this.id === selector.slice(1);
    const m = /^\[([^=\]]+)(?:="([^"]*)")?\]$/.exec(selector);
    if (m) return this.hasAttribute(m[1]) && (m[2] === undefined || this.getAttribute(m[1]) === m[2]);
    return this.tagName === selector.toUpperCase();
  }
  querySelector(selector) {
    for (const c of this.children) {
      if (c.matches(selector)) return c;
      const inner = c.querySelector(selector);
      if (inner) return inner;
    }
    return null;
  }
}

export class Document extends Target {
  constructor(window) {
    super();
    this.window = window;
    this.body = new Element(this, 'body');
    this.body.parentNode = null;
    this.activeElement = this.body;
    this.hidden = false;
    this.title = '';
  }
  createElement(tag) { return new Element(this, tag); }
  createTextNode(t) { return new Text(t); }
  querySelector(selector) { return this.body.matches(selector) ? this.body : this.body.querySelector(selector); }
  getElementById(id) { return this.querySelector('#' + id); }
  // Capture from the window down, the target, then bubbling back up.
  dispatch(target, event) {
    event.target = target;
    const path = [];
    for (let n = target.parentNode; n; n = n.parentNode) path.unshift(n);
    const capture = [this.window, this, ...path];
    for (const t of capture) {
      t.fire(event, 'capture');
      if (event.stopped) return event;
    }
    target.fire(event, 'target');
    if (event.stopped || !bubbles.has(event.type)) return event;
    for (const t of [...path.reverse(), this, this.window]) {
      t.fire(event, 'bubble');
      if (event.stopped) return event;
    }
    return event;
  }
}

export class Window extends Target {
  constructor() {
    super();
    this.devicePixelRatio = 2;
    this.document = new Document(this);
    this.opened = [];     // window.open's: [url, target, features]
    this.assigned = [];   // the addresses this tab went to
    this.location = { assign: (url) => this.assigned.push(url) };
  }
  open(url, target, features) {
    this.opened.push([url, target, features]);
    return null;  // noopener: a browser gives no handle
  }
  dispatchEvent(event) {
    event.target = this;
    this.fire(event, 'capture');
    if (!event.stopped) this.fire(event, 'bubble');
    return !event.defaultPrevented;
  }
}

// localStorage as a browser has it: strings, a quota, keys by index.
export class Storage {
  constructor(quota = 1 << 20) {
    this.map = new Map();
    this.quota = quota;
  }
  get length() { return this.map.size; }
  key(i) { return [...this.map.keys()][i] ?? null; }
  getItem(k) { return this.map.has(k) ? this.map.get(k) : null; }
  setItem(k, v) {
    let used = 0;
    for (const [key, value] of this.map) if (key !== k) used += key.length + value.length;
    if (used + k.length + String(v).length > this.quota) {
      const e = new Error('The quota has been exceeded.');
      e.name = 'QuotaExceededError';
      throw e;
    }
    this.map.set(k, String(v));
  }
  removeItem(k) { this.map.delete(k); }
}
