// The `openmui` ES module's JavaScript: what a script imports. The native
// side (`__omui`, native/js.cpp) appends one builder per registered component.
module openmui.lang.js:prelude;

import std;

namespace omui::lang::js {

inline constexpr std::string_view prelude = R"js(
const N = globalThis.__omui;

export class OmuiError extends Error {
  constructor(message) { super(message); this.name = "OmuiError"; }
}
function native(f, ...args) {
  try { return f(...args); } catch (e) { throw new OmuiError(String(e && e.message !== undefined ? e.message : e)); }
}

const json = (v) => JSON.stringify(v === undefined ? null : v);
const merge = (a, b) => [...new Set([...a, ...b])];

export class Expr {
  constructor(text, signals) { this.text = text; this.signals = signals; }
  field(name) { return new Expr(this.text + "." + name, this.signals); }
  // An element of a list, or a value of a map: list.at(0), map.at("key").
  at(index) { const i = operand(index); return new Expr("(" + this.text + ")[" + i.text + "]", merge(this.signals, i.signals)); }
  toString() { return "Expr(" + this.text + ")"; }
}
function operand(v) {
  if (v instanceof Expr) return v;
  if (v instanceof Signal) return v.expr;
  return new Expr(json(v), []);
}
function bin(op, a, b) {
  a = operand(a); b = operand(b);
  return new Expr("(" + a.text + " " + op + " " + b.text + ")", merge(a.signals, b.signals));
}
function un(op, a) { a = operand(a); return new Expr(op + "(" + a.text + ")", a.signals); }

export const add = (a, b) => bin("+", a, b), sub = (a, b) => bin("-", a, b), mul = (a, b) => bin("*", a, b);
export const div = (a, b) => bin("/", a, b), mod = (a, b) => bin("%", a, b), neg = (a) => un("-", a);
export const eq = (a, b) => bin("==", a, b), ne = (a, b) => bin("!=", a, b), lt = (a, b) => bin("<", a, b);
export const le = (a, b) => bin("<=", a, b), gt = (a, b) => bin(">", a, b), ge = (a, b) => bin(">=", a, b);
export const and = (a, b) => bin("&&", a, b), or = (a, b) => bin("||", a, b), not = (a) => un("!", a);
export function call(name, ...args) {
  const ops = args.map(operand);
  return new Expr(name + "(" + ops.map((a) => a.text).join(", ") + ")", ops.reduce((s, a) => merge(s, a.signals), []));
}
export const fmt = (pattern, ...args) => call("format", pattern, ...args);
export function select(c, a, b) {
  // select(props, children) is the select component's builder; select(cond, a, b) an expression.
  if (arguments.length < 3) return element("select", c, a);
  c = operand(c); a = operand(a); b = operand(b);
  return new Expr("(" + c.text + " ? " + a.text + " : " + b.text + ")", merge(merge(c.signals, a.signals), b.signals));
}
for (const [m, f] of Object.entries({ add, sub, mul, div, mod, eq, ne, lt, le, gt, ge, and, or }))
  Expr.prototype[m] = function (b) { return f(this, b); };
Expr.prototype.not = function () { return not(this); };
Expr.prototype.neg = function () { return neg(this); };

export class Signal {
  constructor(handle) { this.handle = handle; this.expr = new Expr("s" + handle, [handle]); }
  get() { return JSON.parse(N.signalGet(this.handle)); }
  get value() { return this.get(); }
  set value(v) { this.set(v); }
  set(v) { N.signalSet(this.handle, json(v)); }
  update(f) { this.set(f(this.get())); }
  watch(f) { N.signalWatch(this.handle, (t) => f(JSON.parse(t))); }
  toString() { return "Signal(s" + this.handle + ")"; }
}
for (const m of ["add", "sub", "mul", "div", "mod", "eq", "ne", "lt", "le", "gt", "ge", "and", "or", "not", "neg", "field", "at"])
  Signal.prototype[m] = function (...a) { return this.expr[m](...a); };
export function signal(initial, key) { return new Signal(native(N.signalCreate, json(initial), key === undefined ? "" : String(key))); }

class Tr { constructor(key, args) { this.key = key; this.args = args; } }
// tr("inbox.count", { count }): a message, its arguments by name (MessageFormat 2).
export const tr = (key, args = {}) => new Tr(key, Object.keys(args).sort().map((n) => [n, operand(args[n])]));

const catalog = JSON.parse(N.catalog());
const snake = (s) => s.replace(/[A-Z]/g, (c) => "_" + c.toLowerCase());
const bindable = (type, prop) => {
  const c = catalog.components[type];
  return !!(c && c.props[prop] && c.props[prop].bindable);
};

export class Element {
  constructor(fields) { Object.assign(this, fields); }
}
export function element(type, props, children) {
  if (Array.isArray(props) || props instanceof Element) { children = props; props = {}; }
  const el = { type, id: "", props: {}, on: {}, animate: null, children: [], region: null };
  for (const [k, v] of Object.entries(props || {})) {
    if (v === undefined) continue;
    if (k === "id") el.id = String(v);
    else if (k === "animate") el.animate = v;
    else if (k.length > 2 && k.startsWith("on") && typeof v === "function") el.on[snake(k.slice(2)).replace(/^_/, "")] = v;
    else el.props[snake(k)] = v;
  }
  el.children = [children === undefined ? [] : children].flat(Infinity).filter((c) => c !== null && c !== undefined && c !== false);
  for (const c of el.children)
    if (!(c instanceof Element)) throw new OmuiError(type + ": a child is not an element (text({...}), when(...) …)");
  return new Element(el);
}

let depth = 0;
function rowProxy(e) {
  return new Proxy(e, { get(t, k) { if (k in t || typeof k !== "string") return t[k]; return rowProxy(t.field(k)); } });
}
export const when = (cond, then, otherwise) =>
  new Element({ region: { kind: "when", cond: operand(cond), then: [then ?? []].flat(Infinity), else: [otherwise ?? []].flat(Infinity) } });
export function each(model, body, where) {
  const variable = depth ? "row" + (depth + 1) : "row";
  depth++;
  let el;
  try {
    const row = rowProxy(new Expr(variable, []));
    el = body(row);
    // A filter of the row's own fields: (row) => row.done.not().
    if (typeof where === "function") where = where(row);
  } finally { depth--; }
  if (!(el instanceof Element)) throw new OmuiError("each: the function answers one element, the view of a row");
  return new Element({ region: { kind: "each", model, variable, where: where === undefined ? null : operand(where), body: el } });
}
export const slot = (name) => new Element({ region: { kind: "slot", name } });

function prop(type, name, v) {
  if (v instanceof Signal) return bindable(type, name) ? { b: v.handle } : { e: v.expr.text, s: v.expr.signals };
  if (v instanceof Expr) return { e: v.text, s: v.signals };
  if (v instanceof Tr) return { t: v.key, a: Object.fromEntries(v.args.map(([n, x]) => [n, { e: x.text, s: x.signals }])) };
  return { c: v };
}
function serialize(el, handlers) {
  if (el.region) {
    const r = el.region;
    if (r.kind === "when")
      return { region: { kind: "when", cond: { e: r.cond.text, s: r.cond.signals },
                         then: r.then.map((c) => serialize(c, handlers)), else: r.else.map((c) => serialize(c, handlers)) } };
    if (r.kind === "each")
      return { region: { kind: "each", model: r.model, variable: r.variable,
                         where: r.where && { e: r.where.text, s: r.where.signals }, body: serialize(r.body, handlers) } };
    return { region: { kind: "slot", name: r.name } };
  }
  const props = {};
  for (const [k, v] of Object.entries(el.props)) props[k] = prop(el.type, k, v);
  const on = {};
  for (const [e, f] of Object.entries(el.on)) { on[e] = handlers.length; handlers.push(f); }
  return { type: el.type, id: el.id, props, on, animate: el.animate, children: el.children.map((c) => serialize(c, handlers)) };
}

export function mount(slotName, el) {
  if (!(el instanceof Element)) throw new OmuiError("mount(slot, element)");
  const handlers = [];
  native(N.mount, String(slotName), JSON.stringify(serialize(el, handlers)), handlers);
}
export const unmount = (slotName) => native(N.unmount, String(slotName));
export const document = (el) => JSON.parse(native(N.document, JSON.stringify(serialize(el, []))));

function answer(text) {
  const records = JSON.parse(text);
  let data = true;
  for (const r of records) {
    if (r.kind === "error") throw new OmuiError(r.code + ": " + (r.message ?? ""));
    if (r.kind === "data") data = r.data;
  }
  return data;
}
export function verb(name, options, fn) {
  if (typeof options === "function") { fn = options; options = {}; }
  native(N.verb, String(name), json(options ?? {}), (args) => json(fn(JSON.parse(args))));
}
export const invoke = (name, args) => answer(native(N.invoke, String(name), json(args ?? {})));
// The runtime's theme: theme("dark", "compact") or theme(["dark", "compact"]) —
// a theme, then overlays; themes() lists them and the current composition.
export function theme(...layers) {
  const l = layers.length === 1 && Array.isArray(layers[0]) ? layers[0] : layers;
  native(N.theme, json(l.map(String)));
}
export const themes = () => JSON.parse(N.themes());
// A native module's function (component-binding §8), as openmui:native/<name>
// calls it: positional arguments, a result; an `async` one, a promise of it.
export function __native(module, fn, isAsync, args) {
  const a = json(args.map((v) => (v === undefined ? null : v)));
  if (isAsync) return N.nativeAsync(module, fn, a).then((t) => JSON.parse(t), (why) => { throw new OmuiError(String(why)); });
  return JSON.parse(native(N.nativeCall, module, fn, a));
}
export const act = (request) => answer(native(N.act, json(request)));
export const snapshot = (query) => answer(native(N.snapshot, json(query ?? {})));
export function model(name) {
  return {
    rows: () => JSON.parse(native(N.rows, String(name))),
    splice: (from, remove, rows) => native(N.splice, String(name), from, remove, json(rows ?? [])),
    push(rows) { native(N.splice, String(name), this.rows().length, 0, json(rows)); },
    clear() { native(N.splice, String(name), 0, 0x7fffffff, "[]"); },
  };
}
export function find(selector) {
  const handle = native(N.find, typeof selector === "string" ? selector : json(selector));
  return {
    handle,
    get: (p) => JSON.parse(native(N.get, handle, String(p))),
    set: (props) => native(N.set, handle, json(Object.fromEntries(Object.entries(props).map(([k, v]) => [snake(k), v])))),
  };
}
export const log = (...a) => N.log(a.map(String).join(" "));
export const nil = null;
)js";

}  // namespace omui::lang::js
