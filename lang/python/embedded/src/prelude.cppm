// The `openmui` Python module's Python half: what a script imports. It is
// lang/python's python/openmui (host mode) written for PocketPy's subset, so a
// script reads the same in both; the native half is `_openmui`
// (native/runtime.cpp). PocketPy has no module __getattr__: the builders are
// made here, one per component of the runtime's catalogue, when the module is.
module openmui.lang.python.embedded:prelude;

import std;

namespace omui::lang::python::embedded {

inline constexpr std::string_view prelude = R"py(
import builtins as _b
import _openmui as _n

# A component may be named as a builtin (list): the module's own code uses _b.


class OmuiError(Exception):
    """A call openmui refused or could not do; the message says why."""


# ── expressions ─────────────────────────────────────────────────────────────

class Expr:
    """CEL text the runtime evaluates, and the signals it reads."""

    def __init__(self, text, signals=None):
        self.text = text
        self.signals = _b.tuple(signals) if signals is not None else ()

    def __repr__(self):
        return "Expr(" + self.text + ")"

    def __bool__(self):
        raise TypeError("an expression has no truth value in Python: the runtime evaluates it (use & | ~ and select)")

    def __getattr__(self, name):  # row.title: a field
        if name.startswith("__"):
            raise AttributeError(name)
        return Expr(self.text + "." + name, self.signals)

    def __getitem__(self, key):
        k = _operand(key)
        return Expr(self.text + "[" + k.text + "]", _merge(self.signals, k.signals))


def _merge(a, b):
    out = _b.list(a)
    for s in b:
        if s not in out:
            out.append(s)
    return _b.tuple(out)


def _operand(v):
    if _b.isinstance(v, Expr):
        return v
    if _b.isinstance(v, Signal):
        return v.expr
    if _b.isinstance(v, (Element, _Tr)):
        raise OmuiError("an operand is a signal, an expression or data, not " + _b.repr(v))
    return Expr(_n.literal(v))


def _binary(op):
    def apply(a, b):
        a, b = _operand(a), _operand(b)
        return Expr("(" + a.text + " " + op + " " + b.text + ")", _merge(a.signals, b.signals))
    return apply


def _reflected(op):
    f = _binary(op)
    return lambda a, b: f(b, a)


for _name, _op in (("add", "+"), ("sub", "-"), ("mul", "*"), ("truediv", "/"), ("mod", "%"), ("eq", "=="), ("ne", "!="),
                   ("lt", "<"), ("le", "<="), ("gt", ">"), ("ge", ">="), ("and", "&&"), ("or", "||")):
    _b.setattr(Expr, "__" + _name + "__", _binary(_op))
    if _name in ("add", "sub", "mul", "truediv", "mod", "and", "or"):
        _b.setattr(Expr, "__r" + _name + "__", _reflected(_op))
Expr.__neg__ = lambda a: Expr("-(" + a.text + ")", a.signals)
Expr.__invert__ = lambda a: Expr("!(" + a.text + ")", a.signals)

eq, ne, lt, le, gt, ge = [_binary(o) for o in ("==", "!=", "<", "<=", ">", ">=")]
and_, or_ = _binary("&&"), _binary("||")


def not_(a):
    return ~_operand(a)


def call(function, *args):
    """A function of the expression language: ``call("upper", name)``."""
    ops = [_operand(a) for a in args]
    signals = ()
    for o in ops:
        signals = _merge(signals, o.signals)
    return Expr(function + "(" + ", ".join([o.text for o in ops]) + ")", signals)


def fmt(pattern, *args):
    """``fmt("{} clicks", count)``: text the runtime formats as the signals change."""
    return call("format", pattern, *args)


def select(*args, **props):
    """``select(cond, then, otherwise)``: an expression; ``select(props…)``: the select component."""
    if _b.len(args) == 3 and not props:
        c, a, b = [_operand(x) for x in args]
        return Expr("(" + c.text + " ? " + a.text + " : " + b.text + ")", _merge(_merge(c.signals, a.signals), b.signals))
    return _build("select", args, props)


class _Tr:
    def __init__(self, key, args):
        self.key = key
        self.args = {}
        for name, a in _b.sorted(args.items()):
            self.args[name] = _operand(a)


def tr(key, **args):
    """The message of a key in the person's language (MessageFormat 2), its arguments by name:
    ``tr("inbox.count", count=count)``. A key no catalog has is shown as its own message."""
    return _Tr(key, args)


# ── signals ─────────────────────────────────────────────────────────────────

class Signal:
    """A runtime signal, mirrored here: ``get`` never waits; ``set`` reaches the runtime by the next frame."""

    def __init__(self, handle):
        self.handle = handle
        self.expr = Expr("s" + _b.str(handle), (handle,))

    def get(self):
        return _n.signal_get(self.handle)

    def set(self, value):
        _n.signal_set(self.handle, value)

    def update(self, f):
        self.set(f(self.get()))

    def watch(self, f):
        _n.signal_watch(self.handle, f)

    value = _b.property(get, set)

    def __repr__(self):
        return "Signal(s" + _b.str(self.handle) + ")"

    def __bool__(self):
        raise TypeError("a signal has no truth value: use .get(), or an expression (count == 0)")

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return _b.getattr(self.expr, name)


for _name in ("add", "sub", "mul", "truediv", "mod", "eq", "ne", "lt", "le", "gt", "ge", "and", "or",
              "radd", "rsub", "rmul", "rtruediv", "rmod", "rand", "ror", "neg", "invert", "getitem"):
    _b.setattr(Signal, "__" + _name + "__", (lambda n: lambda self, *a: _b.getattr(self.expr, "__" + n + "__")(*a))(_name))


def signal(initial=None, key=None):
    """A signal of the runtime with an initial value. A reload gives the same signal back (by order, or ``key``)."""
    return Signal(_n.signal_create(initial, "" if key is None else _b.str(key)))


# ── elements ────────────────────────────────────────────────────────────────

_stack = []  # the `with` blocks open (the script has one thread)


class Element:
    """A component with its properties, handlers and children; or a region (when, each, slot)."""

    def __init__(self, type="", region=None):
        self.type, self.id = type, ""
        self.props = {}
        self.on = {}
        self.animate = None
        self.children = []
        self.region = region

    def __enter__(self):
        _stack.append(self)
        return self

    def __exit__(self, *exc):
        _stack.pop()
        return False

    def __repr__(self):
        return "Element(" + (self.type or self.region["kind"]) + ")"


def _flatten(items):
    for c in items:
        if _b.isinstance(c, (_b.list, _b.tuple)):
            yield from _flatten(c)
        elif c is not None and c is not False:
            yield c


def _adopt(el):
    # Inside a `with` block an element is a child of the block's element.
    if _stack:
        _stack[-1].children.append(el)
    return el


def _claim(items):
    # Elements given as children: made inside a `with` block first, they are
    # the new element's, not the block's.
    out = []
    for c in _flatten(items):
        if not _b.isinstance(c, Element):
            raise OmuiError("a child is not an element (omui.text(…), omui.when(…) …): " + _b.repr(c))
        if _stack:
            kids = _stack[-1].children
            for i in _b.range(_b.len(kids)):
                if kids[i] is c:
                    del kids[i]
                    break
        out.append(c)
    return out


def _build(type, children, props):
    el = Element(type)
    for k, v in props.items():
        if v is None:
            continue
        if k == "id":
            el.id = _b.str(v)
        elif k == "animate":
            el.animate = v
        elif k.startswith("on_") and _b.callable(v):
            el.on[k[3:]] = v
        else:
            el.props[k] = v
    el.children.extend(_claim(children))
    return _adopt(el)


def element(type, *children, **props):
    """A component by name: ``element("text_box", id="q")`` (the builders do this)."""
    return _build(type, children, props)


def when(cond, then=None, otherwise=None):
    """Children shown while a condition holds (evaluated by the runtime)."""
    return _adopt(Element(region={"kind": "when", "cond": _operand(cond), "then": _claim([then]), "else": _claim([otherwise])}))


_depth = [0]


def each(model, body, where=None):
    """A model's rows: ``each("notes", lambda row: omui.text(text=row.title), where=lambda row: ~row.done)``.

    The body (and a ``where`` given as a function) is built once, with ``row`` an expression."""
    variable = "row" if not _depth[0] else "row" + _b.str(_depth[0] + 1)
    capture = Element("")
    _stack.append(capture)
    _depth[0] += 1
    try:  # PocketPy has no `finally`
        row = Expr(variable)
        el = body(row)
        if _b.callable(where):  # a filter of the row's own fields: where=lambda row: ~row.done
            where = where(row)
    except:
        _depth[0] -= 1
        _stack.pop()
        raise
    _depth[0] -= 1
    _stack.pop()
    if not _b.isinstance(el, Element):
        el = capture.children[0] if _b.len(capture.children) == 1 else None
    if el is None:
        raise OmuiError("each: the function makes one element, the view of a row")
    return _adopt(Element(region={"kind": "each", "model": model, "variable": variable,
                                  "where": None if where is None else _operand(where), "body": el}))


def slot(name):
    """A place code fills."""
    return _adopt(Element(region={"kind": "slot", "name": name}))


_catalog = _n.catalog()


def _bindable(type, prop):
    c = _catalog.get("components", {}).get(type)
    return _b.bool(c and c["props"].get(prop, {}).get("bindable"))


def _expr_json(e):
    return {"e": e.text, "s": _b.list(e.signals)}


def _prop(type, name, v):
    if _b.isinstance(v, Signal):
        return {"b": v.handle} if _bindable(type, name) else _expr_json(v.expr)
    if _b.isinstance(v, Expr):
        return _expr_json(v)
    if _b.isinstance(v, _Tr):
        args = {}
        for n, a in v.args.items():
            args[n] = _expr_json(a)
        return {"t": v.key, "a": args}
    return {"c": v}


def _serialize(el, handlers):
    if el.region is not None:
        r = el.region
        if r["kind"] == "when":
            return {"region": {"kind": "when", "cond": _expr_json(r["cond"]), "then": [_serialize(c, handlers) for c in r["then"]],
                               "else": [_serialize(c, handlers) for c in r["else"]]}}
        if r["kind"] == "each":
            return {"region": {"kind": "each", "model": r["model"], "variable": r["variable"],
                               "where": None if r["where"] is None else _expr_json(r["where"]), "body": _serialize(r["body"], handlers)}}
        return {"region": {"kind": "slot", "name": r["name"]}}
    on = {}
    for event, f in el.on.items():
        on[event] = _b.len(handlers)
        handlers.append(f)
    props = {}
    for k, v in el.props.items():
        props[k] = _prop(el.type, k, v)
    return {"type": el.type, "id": el.id, "props": props, "on": on, "animate": el.animate,
            "children": [_serialize(c, handlers) for c in el.children]}


def _root(view):
    if _b.isinstance(view, Element):
        return view
    if _b.callable(view):
        capture = Element("")
        _stack.append(capture)
        try:
            made = view()
        except:
            _stack.pop()
            raise
        _stack.pop()
        if _b.isinstance(made, Element):
            return made
        if _b.len(capture.children) == 1:
            return capture.children[0]
        raise OmuiError("a view makes one root element; this one made " + _b.str(_b.len(capture.children)))
    raise OmuiError("mount(slot, view): the view is an element or a function that makes one")


def mount(slot_name, view):
    """Shows a view at a slot: an element, or a function that builds one (as ``with`` blocks or by returning it)."""
    handlers = []
    _n.mount(_b.str(slot_name), _serialize(_root(view), handlers), handlers)


def mount_document(slot_name, document):
    """Shows a view document (``*.view.omui.json``: a path, its text, or the data) at a slot."""
    if _b.isinstance(document, (_b.dict, _b.list)):
        _n.mount_document(_b.str(slot_name), document)
    elif _b.isinstance(document, _b.str) and document.lstrip().startswith("{"):
        _n.mount_document(_b.str(slot_name), document)
    else:
        with _b.open(document, "r") as f:
            _n.mount_document(_b.str(slot_name), f.read())


def unmount(slot_name):
    _n.unmount(_b.str(slot_name))


def document(view):
    """The view document a view is, as data (what ``omui check`` reads)."""
    return _n.document(_serialize(_root(view), []))


# ── verbs, models, the semantic tree ─────────────────────────────────────────

def _answer(records):
    data = True
    for r in records:
        if r.get("kind") == "error":
            raise OmuiError(_b.str(r.get("code")) + ": " + _b.str(r.get("message", r.get("text", ""))))
        if r.get("kind") == "data":
            data = r.get("data")
    return data


def verb(name, fn=None, effect="reversible", answer="", summary="", person=False):
    """Registers a verb the script implements (S-1: an irreversible verb states its answer; S-10: a `person` verb
    is the person's own, refused over the bus): ``verb(name, fn, effect=…)``, or as a decorator. A reload registers
    it again: the new function answers, under the same effect and answer."""
    def register(f):
        _n.verb(_b.str(name), {"effect": effect, "answer": answer, "summary": summary, "person": _b.bool(person)}, f)
        return f
    return register(fn) if fn is not None else register


def invoke(name, args=None):
    return _answer(_n.invoke(_b.str(name), {} if args is None else args))


def _fields(base, fields):
    out = {}
    if base:
        for k, v in base.items():
            out[k] = v
    for k, v in fields.items():
        out[k] = v
    return out


def act(request=None, **fields):
    return _answer(_n.act(_fields(request, fields)))


def snapshot(query=None, **fields):
    return _answer(_n.snapshot(_fields(query, fields)))


class _Node:
    def __init__(self, handle):
        self.handle = handle

    def get(self, prop):
        return _n.get(self.handle, prop)

    def set(self, props=None, **fields):
        _n.set(self.handle, _fields(props, fields))


def find(selector):
    return _Node(_n.find(selector))


class _Model:
    def __init__(self, name):
        self.name = name

    def rows(self):
        return _n.rows(self.name)

    def splice(self, start, remove, rows=None):
        _n.splice(self.name, start, remove, [] if rows is None else _b.list(rows))

    def push(self, rows):
        self.splice(_b.len(self.rows()), 0, rows)

    def clear(self):
        self.splice(0, 0x7FFFFFFF, [])


def model(name):
    """A model's rows: ``model("notes").push([{"key": "n1", "title": "Milk"}])``."""
    return _Model(name)


def theme(*layers):
    """The runtime's theme: a theme, then overlays — ``omui.theme("dark", "compact")``. A refusal is OmuiError; the theme stays."""
    if _b.len(layers) == 1 and _b.isinstance(layers[0], (_b.list, _b.tuple)):
        layers = layers[0]
    _n.theme([_b.str(l) for l in layers])


def themes():
    """The themes and overlays there are, and the composition now: ``{"themes", "overlays", "current", "dark"}``."""
    return _n.themes()


def log(*values):
    _n.log(" ".join([_b.str(v) for v in values]))


def run(view=None, title="openmui", size=None, headless=None, png=None, frames=None, argv=None):
    """Host mode's entry, embedded: the program owns the window and the frames, so this mounts the view at "main"
    and answers 0 (a script written for host mode runs here unchanged)."""
    if view is not None:
        mount("main", view)
    return 0


# Native modules (component-binding §8): omui.native.fuzzy, or `from openmui.native import fuzzy`;
# `native` is the module openmui.native, made by the host before this runs.
_KEYWORDS = ("match",)  # names PocketPy reserves that CPython does not


class _NativeModule:
    """A native module: its functions as attributes."""

    def __init__(self, name, spec):
        self.__name__ = name
        self.__doc__ = spec.get("doc", "")
        for function in spec.get("functions", {}):
            f = _native_function(name, function)
            _b.setattr(self, function, f)
            if function in _KEYWORDS:  # fuzzy.match_: `match` is a keyword of PocketPy's
                _b.setattr(self, function + "_", f)

    def __repr__(self):
        return "<openmui native module " + self.__name__ + ">"


def _native_function(module, function):
    def call(*args, **kwargs):
        if args and kwargs:
            raise TypeError(module + "." + function + ": arguments by position or by name, not both")
        return _n.native(module, function, _b.list(args) if not kwargs else kwargs)
    return call


for _name, _spec in _n.natives().get("modules", {}).items():
    _b.setattr(native, _name, _NativeModule(_name, _spec))


# omui.text, omui.text_box …: a builder for every registered component (the
# module's own names win: select is both).
import openmui as _self


def _builder(name):
    def build(*children, **props):
        return _build(name, children, props)
    return build


for _name in _catalog.get("components", {}):
    if _b.getattr(_self, _name, None) is None:
        _b.setattr(_self, _name, _builder(_name))
)py";

}  // namespace omui::lang::python::embedded
