"""openmui for Python (component-binding §5.2, §7).

    import openmui as omui

    count = omui.signal(0)

    def counter():
        with omui.column(gap=8):
            omui.text(text=omui.fmt("{} clicks", count))
            omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))

    omui.run(counter, title="Counter")

- Builders: one for every registered component, by its contract name
  (``omui.text_box``), as calls (``omui.row(child, child, gap=8)``) or as
  context managers (``with omui.column(): …``). Handlers are
  ``on_<event>=callable``; a signal given to a property binds two-way where
  the property is bindable and one-way elsewhere.
- Signals (``signal(v)``: ``get``, ``set``, ``update``, ``watch``,
  ``value``) are the runtime's, mirrored: reading one never waits.
- Expressions, evaluated by the runtime (no Python runs in a frame):
  operators on signals and expressions (``count + 1``, ``count == 0``,
  ``a & b``, ``~a``, ``row.title``), and ``fmt``, ``call``, ``select``,
  ``tr``.
- Regions: ``when``, ``each(model, lambda row: …)``, ``slot``.
- ``mount``, ``verb`` / ``invoke``, ``model``, ``find`` / ``act`` /
  ``snapshot``; ``run`` shows the application (``--headless``, ``--png``
  as for every openmui application).

Python runs on its own thread beside the runtime's; a failing call raises
``OmuiError``.
"""

import builtins as _builtins
import json as _json
import os as _os
import sys as _sys
import threading as _threading

from . import _openmui as _n

__all__ = [
    "OmuiError", "Expr", "Signal", "Element", "signal", "fmt", "call", "select", "tr", "eq", "ne", "lt", "le", "gt", "ge",
    "and_", "or_", "not_", "when", "each", "slot", "element", "mount", "mount_document", "unmount", "document", "verb", "invoke", "act",
    "snapshot", "model", "find", "run", "settle", "step", "png", "pixels", "log", "reload",
]


class OmuiError(RuntimeError):
    """A call openmui refused or could not do; the message says why."""


def _native(f, *args):
    try:
        return f(*args)
    except RuntimeError as e:
        raise OmuiError(str(e)) from None


def _dump(v):
    return _json.dumps(v, ensure_ascii=False)


def _listener(f):
    # A handler or a watch, as the runtime calls it: the event's JSON in, and
    # nothing back -- what the function answers is not the runtime's.
    def listen(text):
        f(_json.loads(text))
    return listen


# ── expressions ─────────────────────────────────────────────────────────────

class Expr:
    """CEL text the runtime evaluates, and the signals it reads."""

    __slots__ = ("text", "signals")

    def __init__(self, text, signals=()):
        self.text = text
        self.signals = _builtins.tuple(signals)

    def __repr__(self):
        return f"Expr({self.text})"

    def __bool__(self):
        raise TypeError("an expression has no truth value in Python: the runtime evaluates it (use & | ~ and select)")

    __hash__ = None

    def __getattr__(self, name):  # row.title: a field
        if name.startswith("__"):
            raise AttributeError(name)
        return Expr(f"{self.text}.{name}", self.signals)

    def __getitem__(self, key):
        k = _operand(key)
        return Expr(f"{self.text}[{k.text}]", _merge(self.signals, k.signals))


def _merge(a, b):
    out = _builtins.list(a)
    for s in b:
        if s not in out:
            out.append(s)
    return _builtins.tuple(out)


def _operand(v):
    if isinstance(v, Expr):
        return v
    if isinstance(v, Signal):
        return v.expr
    return Expr(_dump(v))


def _binary(op):
    def apply(a, b):
        a, b = _operand(a), _operand(b)
        return Expr(f"({a.text} {op} {b.text})", _merge(a.signals, b.signals))
    return apply


def _reflected(op):
    f = _binary(op)
    return lambda a, b: f(b, a)


for _name, _op in (("add", "+"), ("sub", "-"), ("mul", "*"), ("truediv", "/"), ("mod", "%"), ("eq", "=="), ("ne", "!="),
                   ("lt", "<"), ("le", "<="), ("gt", ">"), ("ge", ">="), ("and", "&&"), ("or", "||")):
    setattr(Expr, f"__{_name}__", _binary(_op))
    if _name in ("add", "sub", "mul", "truediv", "mod", "and", "or"):
        setattr(Expr, f"__r{_name}__", _reflected(_op))
Expr.__neg__ = lambda a: Expr(f"-({a.text})", a.signals)
Expr.__invert__ = lambda a: Expr(f"!({a.text})", a.signals)

eq, ne, lt, le, gt, ge = (_binary(o) for o in ("==", "!=", "<", "<=", ">", ">="))
and_, or_ = _binary("&&"), _binary("||")


def not_(a):
    return ~_operand(a)


def call(function, *args):
    """A function of the expression language: ``call("upper", name)``."""
    ops = [_operand(a) for a in args]
    signals = ()
    for o in ops:
        signals = _merge(signals, o.signals)
    return Expr(f"{function}({', '.join(o.text for o in ops)})", signals)


def fmt(pattern, *args):
    """``fmt("{} clicks", count)``: text the runtime formats as the signals change."""
    return call("format", pattern, *args)


def select(*args, **props):
    """``select(cond, then, otherwise)``: an expression; ``select(props…)``: the select component."""
    if len(args) == 3 and not props:
        c, a, b = (_operand(x) for x in args)
        return Expr(f"({c.text} ? {a.text} : {b.text})", _merge(_merge(c.signals, a.signals), b.signals))
    return _build("select", args, props)


class _Tr:
    __slots__ = ("key", "args")

    def __init__(self, key, args):
        self.key, self.args = key, [_operand(a) for a in args]


def tr(key, *args):
    """The message of a key in the current locale, formatted with the arguments."""
    return _Tr(key, args)


# ── signals ─────────────────────────────────────────────────────────────────

class Signal:
    """A runtime signal, mirrored here: ``get`` never waits; ``set`` reaches the runtime by the next frame."""

    __slots__ = ("handle", "expr")

    def __init__(self, handle):
        self.handle = handle
        self.expr = Expr(f"s{handle}", (handle,))

    def get(self):
        return _json.loads(_n.signal_get(self.handle))

    def set(self, value):
        _n.signal_set(self.handle, _dump(value))

    def update(self, f):
        self.set(f(self.get()))

    def watch(self, f):
        _n.signal_watch(self.handle, _listener(f))

    value = property(get, set)

    def __repr__(self):
        return f"Signal(s{self.handle})"

    def __bool__(self):
        raise TypeError("a signal has no truth value: use .get(), or an expression (count == 0)")

    __hash__ = object.__hash__

    def __getattr__(self, name):
        if name.startswith("__"):
            raise AttributeError(name)
        return getattr(self.expr, name)


for _name in ("add", "sub", "mul", "truediv", "mod", "eq", "ne", "lt", "le", "gt", "ge", "and", "or",
              "radd", "rsub", "rmul", "rtruediv", "rmod", "rand", "ror", "neg", "invert", "getitem"):
    setattr(Signal, f"__{_name}__", (lambda n: lambda self, *a: getattr(self.expr, f"__{n}__")(*a))(_name))


def signal(initial=None, key=None):
    """A signal of the runtime with an initial value. A reload gives the same signal back (by order, or ``key``)."""
    return Signal(_native(_n.signal_create, _dump(initial), "" if key is None else str(key)))


# ── elements ────────────────────────────────────────────────────────────────

_local = _threading.local()


def _stack():
    s = getattr(_local, "stack", None)
    if s is None:
        s = _local.stack = []
    return s


class Element:
    """A component with its properties, handlers and children; or a region (when, each, slot)."""

    __slots__ = ("type", "id", "props", "on", "animate", "children", "region")

    def __init__(self, type="", id="", props=None, on=None, animate=None, children=None, region=None):
        self.type, self.id = type, id
        self.props = props or {}
        self.on = on or {}
        self.animate = animate
        self.children = children if children is not None else []
        self.region = region

    def __enter__(self):
        _stack().append(self)
        return self

    def __exit__(self, *exc):
        _stack().pop()
        return False

    def __repr__(self):
        return f"Element({self.type or self.region['kind']})"


def _flatten(items):
    for c in items:
        if isinstance(c, (_builtins.list, _builtins.tuple)):
            yield from _flatten(c)
        elif c is not None and c is not False:
            yield c


def _adopt(el):
    # Inside a `with` block an element is a child of the block's element.
    s = _stack()
    if s:
        s[-1].children.append(el)
    return el


def _claim(items):
    # Elements given as children: made inside a `with` block first, they are
    # the new element's, not the block's.
    out = []
    s = _stack()
    for c in _flatten(items):
        if not isinstance(c, Element):
            raise OmuiError("a child is not an element (omui.text(…), omui.when(…) …)")
        if s and any(c is x for x in s[-1].children):
            s[-1].children.remove(c)
        out.append(c)
    return out


def _build(type, children, props):
    el = Element(type)
    for k, v in props.items():
        if v is None:
            continue
        if k == "id":
            el.id = str(v)
        elif k == "animate":
            el.animate = v
        elif k.startswith("on_") and callable(v):
            el.on[k[3:]] = v
        else:
            el.props[k] = v
    el.children.extend(_claim(children))
    return _adopt(el)


def element(type, *children, **props):
    """A component by name: ``element("text_box", id="q")`` (the builders do this)."""
    return _build(type, children, props)


def when(cond, then=(), otherwise=()):
    """Children shown while a condition holds (evaluated by the runtime)."""
    return _adopt(Element(region={"kind": "when", "cond": _operand(cond), "then": _claim([then]), "else": _claim([otherwise])}))


_depth = [0]


def each(model, body, where=None):
    """A model's rows: ``each("notes", lambda row: omui.text(text=row.title))``; built once, with ``row`` an expression."""
    variable = "row" if not _depth[0] else f"row{_depth[0] + 1}"
    capture = Element("")
    _stack().append(capture)
    _depth[0] += 1
    try:
        el = body(Expr(variable))
    finally:
        _depth[0] -= 1
        _stack().pop()
    if not isinstance(el, Element):
        el = capture.children[0] if len(capture.children) == 1 else None
    if el is None:
        raise OmuiError("each: the function makes one element, the view of a row")
    return _adopt(Element(region={"kind": "each", "model": model, "variable": variable,
                                  "where": None if where is None else _operand(where), "body": el}))


def slot(name):
    """A place code fills."""
    return _adopt(Element(region={"kind": "slot", "name": name}))


_catalog = None


def _components():
    global _catalog
    if _catalog is None:
        _catalog = _json.loads(_n.catalog())
    return _catalog["components"]


def _bindable(type, prop):
    c = _components().get(type)
    return bool(c and c["props"].get(prop, {}).get("bindable"))


def _prop(type, name, v):
    if isinstance(v, Signal):
        return {"b": v.handle} if _bindable(type, name) else {"e": v.expr.text, "s": _builtins.list(v.expr.signals)}
    if isinstance(v, Expr):
        return {"e": v.text, "s": _builtins.list(v.signals)}
    if isinstance(v, _Tr):
        return {"t": v.key, "a": [{"e": a.text, "s": _builtins.list(a.signals)} for a in v.args]}
    return {"c": v}


def _expr_json(e):
    return {"e": e.text, "s": _builtins.list(e.signals)}


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
        on[event] = len(handlers)
        handlers.append(_listener(f))
    return {"type": el.type, "id": el.id, "props": {k: _prop(el.type, k, v) for k, v in el.props.items()}, "on": on,
            "animate": el.animate, "children": [_serialize(c, handlers) for c in el.children]}


def _root(view):
    if isinstance(view, Element):
        return view
    if callable(view):
        capture = Element("")
        _stack().append(capture)
        try:
            made = view()
        finally:
            _stack().pop()
        if isinstance(made, Element):
            return made
        if len(capture.children) == 1:
            return capture.children[0]
        raise OmuiError(f"a view makes one root element; this one made {len(capture.children)}")
    raise OmuiError("mount(slot, view): the view is an element or a function that makes one")


def mount(slot_name, view):
    """Shows a view at a slot: an element, or a function that builds one (as ``with`` blocks or by returning it)."""
    handlers = []
    tree = _dump(_serialize(_root(view), handlers))
    _native(_n.mount, str(slot_name), tree, handlers)


def mount_document(slot_name, document):
    """Shows a view document (``*.view.omui.json``: a path, its text, or the data) at a slot."""
    if isinstance(document, (dict, _builtins.list)):
        text = _dump(document)
    elif isinstance(document, str) and document.lstrip().startswith("{"):
        text = document
    else:
        with open(document, encoding="utf-8") as f:
            text = f.read()
    _native(_n.mount_document, str(slot_name), text)


def unmount(slot_name):
    _native(_n.unmount, str(slot_name))


def document(view):
    """The view document a view is, as data (what ``omui check`` reads)."""
    return _json.loads(_native(_n.document, _dump(_serialize(_root(view), []))))


# ── verbs, models, the semantic tree ─────────────────────────────────────────

def _answer(text):
    data = True
    for r in _json.loads(text):
        if r.get("kind") == "error":
            raise OmuiError(f"{r.get('code')}: {r.get('message', '')}")
        if r.get("kind") == "data":
            data = r.get("data")
    return data


def verb(name, fn=None, *, effect="reversible", answer="", summary=""):
    """Registers a verb the script implements (S-1: an irreversible verb states its answer). Usable as a decorator."""
    def register(f):
        _native(_n.verb, name, _dump({"effect": effect, "answer": answer, "summary": summary}),
                lambda text: _dump(f(_json.loads(text))))
        return f
    return register(fn) if fn is not None else register


def invoke(name, args=None):
    return _answer(_native(_n.invoke, name, _dump(args or {})))


def act(request=None, **fields):
    return _answer(_native(_n.act, _dump({**(request or {}), **fields})))


def snapshot(query=None, **fields):
    return _answer(_native(_n.snapshot, _dump({**(query or {}), **fields})))


class _Node:
    __slots__ = ("handle",)

    def __init__(self, handle):
        self.handle = handle

    def get(self, prop):
        return _json.loads(_native(_n.get, self.handle, prop))

    def set(self, **props):
        _native(_n.set, self.handle, _dump(props))


def find(selector):
    return _Node(_native(_n.find, selector if isinstance(selector, str) else _dump(selector)))


class _Model:
    __slots__ = ("name",)

    def __init__(self, name):
        self.name = name

    def rows(self):
        return _json.loads(_native(_n.rows, self.name))

    def splice(self, start, remove, rows=()):
        _native(_n.splice, self.name, start, remove, _dump(_builtins.list(rows)))

    def push(self, rows):
        self.splice(len(self.rows()), 0, rows)

    def clear(self):
        self.splice(0, 0x7FFFFFFF, [])


def model(name):
    """A model's rows: ``model("notes").push([{"key": "n1", "title": "Milk"}])``."""
    return _Model(name)


# ── running ─────────────────────────────────────────────────────────────────

def run(view=None, *, title="openmui", size=(960, 640), headless=None, png=None, frames=None, argv=None):
    """Shows the application: a window, or ``--headless`` (``--png FILE``, ``--frames N``) from the command line."""
    args = _builtins.list(_sys.argv[1:] if argv is None else argv)
    options = {"title": title, "size": _builtins.list(size), "headless": bool(headless) or "--headless" in args
               or _os.environ.get("OMUI_HEADLESS") == "1"}
    if png is None and "--png" in args and args.index("--png") + 1 < len(args):
        png = args[args.index("--png") + 1]
    if png is not None:
        options["png"] = str(png)
        options["headless"] = True
    if frames is not None:
        options["frames"] = int(frames)
    if view is not None:
        mount("main", view)
    return _n.run(_dump(options))


def settle():
    """Frames until the script has nothing left to do (headless; tests)."""
    _native(_n.settle)


def step(frames=1):
    _native(_n.step, int(frames))


def png(file):
    """The last frame's pixels, as a PNG file (headless)."""
    _native(_n.png, str(file))


def pixels():
    """The last frame's pixels as the hash tests pin: FNV-1a over the RGBA rows, 16 hex digits (headless)."""
    return _native(_n.pixels)


def log(*values):
    _n.log(" ".join(str(v) for v in values))


def reload():
    """The script is run again (hot reload): its signals keep their values."""
    _n.reload()


def __getattr__(name):
    # omui.text, omui.text_box …: a builder for every registered component.
    if name.startswith("_"):
        raise AttributeError(name)
    if name in _components():
        def build(*children, **props):
            return _build(name, children, props)
        build.__name__ = name
        build.__doc__ = _components()[name].get("doc", "")
        return build
    raise AttributeError(f"module 'openmui' has no attribute '{name}' (and no component is named so)")
