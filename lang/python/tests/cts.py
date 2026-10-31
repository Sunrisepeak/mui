"""The CTS level S runner of Python (spec/conformance/S/README.md): a
scenario's view built with the Python builders, operators and closures, its
steps done through the Python API, and the trace they record."""

import os

import openmui as omui

SCENARIOS = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "spec", "conformance", "S"))

# Python's own operators: an expression (or a signal) on either side makes an
# expression, the reflected operator when it is on the right.
_BINARY = {
    "+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b, "/": lambda a, b: a / b,
    "%": lambda a, b: a % b, "==": lambda a, b: a == b, "!=": lambda a, b: a != b, "<": lambda a, b: a < b,
    "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b,
    "and": lambda a, b: a & b, "or": lambda a, b: a | b,
}


_verbs = {}  # name → what it does in the scenario that runs now


def _native(name):
    """`fuzzy.rank` through the generated binding: omui.native.fuzzy.rank."""
    module, function = name.split(".")
    return getattr(getattr(omui.native, module), function)


class Runner:
    def __init__(self, scenario):
        self.s = scenario["scenario"]
        self.signals = {}

    # ── the view ───────────────────────────────────────────────────────────
    def operand(self, v, row=None):
        if not isinstance(v, dict):
            return v
        if "const" in v:
            return v["const"]
        if "signal" in v:
            return self.signals[v["signal"]]
        if "row" in v:
            return getattr(row, v["row"])
        if "op" in v:
            args = [self.operand(a, row) for a in v["args"]]
            if v["op"] == "not":
                return ~args[0]
            if v["op"] == "neg":
                return -args[0]
            return _BINARY[v["op"]](*args)
        if "call" in v:
            return omui.call(v["call"], *[self.operand(a, row) for a in v["args"]])
        if "fmt" in v:
            return omui.fmt(v["fmt"], *[self.operand(a, row) for a in v.get("args", [])])
        if "select" in v:
            return omui.select(*[self.operand(x, row) for x in v["select"]])
        raise ValueError(f"an operand cannot be {v!r}")

    def element(self, e, row=None):
        if "when" in e:
            return omui.when(self.operand(e["when"], row), [self.element(c, row) for c in e.get("then", [])],
                             [self.element(c, row) for c in e.get("else", [])])
        if "each" in e:
            where = e.get("where")
            return omui.each(e["each"], lambda r: self.element(e["body"], r),
                             where=None if where is None else (lambda r: self.operand(where, r)))
        props = {k: self.operand(v, row) for k, v in e.get("props", {}).items()}
        if "id" in e:
            props["id"] = e["id"]
        for event, actions in e.get("on", {}).items():
            props["on_" + event] = self.handler(actions)
        return getattr(omui, e["type"])(*[self.element(c, row) for c in e.get("children", [])], **props)

    def handler(self, actions):
        def handle(event):
            for a in actions:
                self.action(a, event=event)
        return handle

    # ── actions ────────────────────────────────────────────────────────────
    def value(self, v, event=None, args=None):
        if not isinstance(v, dict):
            return v
        if "const" in v:
            return v["const"]
        if "event" in v:
            return event[v["event"]]
        if "row" in v:
            return event["rows"]["row"][v["row"]]
        if "arg" in v:
            return args[v["arg"]]
        if "get" in v:
            return self.signals[v["get"]].get()
        return {k: self.value(x, event, args) for k, x in v.items()}

    def action(self, a, event=None, args=None):
        def val(v):
            return self.value(v, event, args)

        if "native" in a:
            self.signals[a["into"]].set(_native(a["native"])(*[val(x) for x in a.get("args", [])]))
        elif "set" in a and "to" in a:
            self.signals[a["set"]].set(val(a["to"]))
        elif "add" in a:
            by, lo, hi = a["by"], a.get("min"), a.get("max")

            def bump(v):
                v += by
                v = v if lo is None else max(v, lo)
                return v if hi is None else min(v, hi)
            self.signals[a["add"]].update(bump)
        elif "invoke" in a:
            omui.invoke(a["invoke"], {k: val(x) for k, x in a.get("args", {}).items()})
        elif "push" in a:
            omui.model(a["push"]).push([{k: val(x) for k, x in a["row"].items()}])
        elif "remove" in a or "change" in a:
            model = omui.model(a.get("remove") or a.get("change"))
            key = val(a["key"])
            rows = model.rows()
            i = next(i for i, r in enumerate(rows) if r["key"] == key)
            changed = [] if "remove" in a else [{**rows[i], **{k: val(x) for k, x in a["set"].items()}}]
            model.splice(i, 1, changed)
        else:
            raise ValueError(f"an action cannot be {a!r}")

    # ── the scenario ───────────────────────────────────────────────────────
    def verb(self, d):
        def answer(args):
            for a in d.get("do", []):
                self.action(a, args=args)
            return self.value(d.get("result"), args=args)
        return answer

    def load(self):
        for name in sorted(self.s.get("signals", {})):
            self.signals[name] = omui.signal(self.s["signals"][name])
        # One world runs the whole suite: a model starts with the scenario's rows only.
        for name, rows in sorted(self.s.get("models", {}).items()):
            omui.model(name).clear()
            omui.model(name).push(rows)
        # A verb is registered once in a world (a second registration is
        # refused); the runner's answers with the current scenario's definition.
        for name, d in sorted(self.s.get("verbs", {}).items()):
            if name not in _verbs:
                omui.verb(name, (lambda n: lambda args: _verbs[n](args))(name), effect=d.get("effect", "reversible"),
                          answer=d.get("answer", ""), summary=d.get("summary", ""), person=d.get("person", False))
            _verbs[name] = self.verb(d)
        omui.mount("main", self.element(self.s["view"]))

    def step(self, i):
        st = self.s["steps"][i]
        try:
            if "tree" in st:
                return {"step": i, "tree": omui.snapshot(root=st["tree"])["nodes"]}
            if "act" in st:
                omui.act(node=st["act"], action=st["action"], args=st.get("args", {}), **({"dry_run": True} if st.get("dry_run") else {}))
                return {"step": i, "act": "ok"}
            if "invoke" in st:
                return {"step": i, "invoke": omui.invoke(st["invoke"], st.get("args", {}))}
            if "get" in st:
                return {"step": i, "get": self.signals[st["get"]].get()}
            if "rows" in st:
                return {"step": i, "rows": omui.model(st["rows"]).rows()}
            if "theme" in st:
                omui.theme(*st["theme"])
                return {"step": i, "theme": "ok"}
            if "themes" in st:
                return {"step": i, "themes": omui.themes()}
            if "native" in st and "into" not in st:
                try:
                    function = _native(st["native"])
                except AttributeError:
                    return {"step": i, "error": "not-found"}  # the binding has no such function
                return {"step": i, "native": function(*st.get("args", []))}
        except omui.OmuiError as e:
            return {"step": i, "error": str(e).split(":", 1)[0]}
        self.action(st)
        return None

    def trace(self):
        """Loads the scenario, does its steps (the runtime settled after each) and answers the records."""
        self.load()
        omui.settle()
        out = []
        for i in range(len(self.s["steps"])):
            r = self.step(i)
            omui.settle()
            if r is not None:
                out.append(r)
        omui.unmount("main")
        if any("theme" in st for st in self.s["steps"]):
            omui.theme("light")  # the world goes on: as it was
        for name in self.s.get("models", {}):
            omui.model(name).clear()
        return out


def canonical(v):
    """A trace as it is compared (spec/conformance/S/README.md): trees without bounds, empty lists and maps alike."""
    if isinstance(v, dict):
        if not v:
            return []
        return {k: canonical(x) for k, x in v.items() if k != "bounds"}
    if isinstance(v, list):
        return [canonical(x) for x in v]
    return v


def expectations(scenario, trace):
    """What the steps' `expect` says that the trace does not show: one line each."""
    out = []
    by_step = {r["step"]: r for r in trace}
    for i, st in enumerate(scenario["scenario"]["steps"]):
        if "expect" not in st:
            continue
        r = by_step.get(i, {})
        if "tree" in st:
            nodes = {n["id"]: n for n in r.get("tree", [])}
            for node, fields in st["expect"].items():
                for k, want in fields.items():
                    got = nodes.get(node, {}).get(k)
                    if got != want:
                        out.append(f"step {i}: {node}.{k} is {got!r}, not {want!r}")
        else:
            got = next((r[k] for k in ("act", "invoke", "get", "rows", "native", "theme", "themes", "error") if k in r), None)
            if got != st["expect"]:
                out.append(f"step {i}: {got!r}, not {st['expect']!r}")
    return out
