// The CTS level S runner of JavaScript (spec/conformance/S/README.md): a
// scenario's view built with the JavaScript builders, operators and closures,
// its steps done through the JavaScript API. The test (js_test.cpp, through
// openmui.cts) calls the two verbs it registers: cts.load with the scenario,
// then cts.step with each step's index, the runtime settling between them; a
// step's answer is its record, or nothing.
import * as omui from "openmui";
import * as fuzzy from "openmui:native/fuzzy";

// The native modules the scenarios use, through the generated bindings.
const natives = { fuzzy };
const nativeFunction = (name, sync) => {
  const [module, fn] = name.split(".");
  const m = natives[module];
  return m && ((sync && m[camel(fn) + "Sync"]) || m[camel(fn)]);
};

let S = null;
const signals = {};

const camel = (s) => s.replace(/_([a-z])/g, (_, c) => c.toUpperCase());
const binary = { "+": "add", "-": "sub", "*": "mul", "/": "div", "%": "mod", "==": "eq", "!=": "ne", "<": "lt", "<=": "le", ">": "gt", ">=": "ge", and: "and", or: "or" };
const codeOf = (e) => String(e && e.message !== undefined ? e.message : e).split(":")[0];

// ── the view ──────────────────────────────────────────────────────────────
function operand(v, row) {
  if (v === null || typeof v !== "object") return v;
  if ("const" in v) return v.const;
  if ("signal" in v) return signals[v.signal];
  if ("row" in v) return row[v.row];
  const args = (v.args ?? []).map((a) => operand(a, row));
  if (v.op === "not") return omui.not(args[0]);
  if (v.op === "neg") return omui.neg(args[0]);
  if (v.op) return omui[binary[v.op]](args[0], args[1]);
  if ("call" in v) return omui.call(v.call, ...args);
  if ("fmt" in v) return omui.fmt(v.fmt, ...args);
  if ("select" in v) return omui.select(...v.select.map((x) => operand(x, row)));
  throw new Error("an operand cannot be " + JSON.stringify(v));
}

function element(e, row) {
  if ("when" in e)
    return omui.when(operand(e.when, row), (e.then ?? []).map((c) => element(c, row)), (e.else ?? []).map((c) => element(c, row)));
  if ("each" in e)
    return omui.each(e.each, (r) => element(e.body, r), e.where === undefined ? undefined : (r) => operand(e.where, r));
  const props = {};
  for (const [k, v] of Object.entries(e.props ?? {})) props[camel(k)] = operand(v, row);
  if (e.id !== undefined) props.id = e.id;
  // A handler awaits what an action promises (an async native).
  for (const [event, actions] of Object.entries(e.on ?? {}))
    props["on" + camel("_" + event)] = async (ev) => { for (const a of actions) await action(a, ev); };
  return omui[camel(e.type)](props, (e.children ?? []).map((c) => element(c, row)));
}

// ── actions ───────────────────────────────────────────────────────────────
function value(v, event, args) {
  if (v === null || typeof v !== "object") return v;
  if ("const" in v) return v.const;
  if ("event" in v) return event[v.event];
  if ("row" in v) return event.rows.row[v.row];
  if ("arg" in v) return args[v.arg];
  if ("get" in v) return signals[v.get].get();
  return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, value(x, event, args)]));
}

function action(a, event, args) {
  const val = (v) => value(v, event, args);
  const map = (o) => Object.fromEntries(Object.entries(o ?? {}).map(([k, x]) => [k, val(x)]));
  if ("native" in a) {
    const into = signals[a.into];
    return Promise.resolve(nativeFunction(a.native, false)(...(a.args ?? []).map(val))).then((r) => into.set(r));
  }
  if ("set" in a && "to" in a) signals[a.set].set(val(a.to));
  else if ("add" in a)
    signals[a.add].update((v) => {
      v += a.by;
      if (a.min !== undefined) v = Math.max(v, a.min);
      if (a.max !== undefined) v = Math.min(v, a.max);
      return v;
    });
  else if ("invoke" in a) omui.invoke(a.invoke, map(a.args));
  else if ("push" in a) omui.model(a.push).push([map(a.row)]);
  else if ("remove" in a || "change" in a) {
    const m = omui.model(a.remove ?? a.change);
    const rows = m.rows();
    const key = val(a.key);
    const i = rows.findIndex((r) => r.key === key);
    if (i < 0) throw new Error("no row " + key);
    m.splice(i, 1, "change" in a ? [{ ...rows[i], ...map(a.set) }] : []);
  } else throw new Error("an action cannot be " + JSON.stringify(a));
}

// ── the scenario ──────────────────────────────────────────────────────────
omui.verb("cts.load", { summary: "Load a CTS scenario" }, (scenario) => {
  S = scenario.scenario;
  for (const name of Object.keys(S.signals ?? {}).sort()) signals[name] = omui.signal(S.signals[name]);
  for (const name of Object.keys(S.models ?? {}).sort()) omui.model(name).push(S.models[name]);
  for (const name of Object.keys(S.verbs ?? {}).sort()) {
    const d = S.verbs[name];
    omui.verb(name, { effect: d.effect ?? "reversible", answer: d.answer ?? "", summary: d.summary ?? "" }, (args) => {
      for (const a of d.do ?? []) action(a, undefined, args);
      return value(d.result, undefined, args);
    });
  }
  omui.mount("main", element(S.view));
  return true;
});

omui.verb("cts.step", { summary: "Do a step of the loaded CTS scenario" }, ({ step: i }) => {
  const st = S.steps[i];
  try {
    if ("tree" in st) return { step: i, tree: omui.snapshot({ root: st.tree }).nodes };
    if ("act" in st) {
      omui.act({ node: st.act, action: st.action, ...(st.args && Object.keys(st.args).length ? { args: st.args } : {}) });
      return { step: i, act: "ok" };
    }
    if ("invoke" in st) return { step: i, invoke: omui.invoke(st.invoke, st.args ?? {}) };
    if ("get" in st) return { step: i, get: signals[st.get].get() };
    if ("rows" in st) return { step: i, rows: omui.model(st.rows).rows() };
    if ("native" in st && !("into" in st)) {
      const fn = nativeFunction(st.native, true);  // a verb cannot await: the Sync form
      if (!fn) return { step: i, error: "not-found" };  // the binding has no such function
      return { step: i, native: fn(...(st.args ?? [])) };
    }
  } catch (e) {
    if (e instanceof omui.OmuiError) return { step: i, error: codeOf(e) };
    throw e;
  }
  action(st);
  return null;
});
