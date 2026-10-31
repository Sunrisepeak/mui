"""The CTS level S runner of embedded Python (spec/conformance/S/README.md): a
scenario's view built with the Python builders, operators and closures, its
steps done through the Python API. The test (embedded_test.cpp, through
openmui.cts) calls the two verbs it registers: cts.load with the scenario,
then cts.step with each step's index, the runtime settling between them; a
step's answer is its record, or nothing. It is lang/lua's tests/cts.lua in
Python, in PocketPy's subset (no generator expressions, no `{**a}`, no `dict(a)`)."""

import openmui as omui

state = {"scenario": None}
signals = {}

# Python's own operators: an expression (or a signal) on either side makes an
# expression, the reflected operator when it is on the right.
BINARY = {
    "+": lambda a, b: a + b, "-": lambda a, b: a - b, "*": lambda a, b: a * b, "/": lambda a, b: a / b,
    "%": lambda a, b: a % b, "==": omui.eq, "!=": omui.ne, "<": omui.lt, "<=": omui.le, ">": omui.gt, ">=": omui.ge,
    "and": omui.and_, "or": omui.or_,
}


# ── the view ──────────────────────────────────────────────────────────────

def operand(v, row=None):
    if not isinstance(v, dict):
        return v
    if "const" in v:
        return v["const"]
    if "signal" in v:
        return signals[v["signal"]]
    if "row" in v:
        return getattr(row, v["row"])
    args = [operand(a, row) for a in v.get("args", [])]
    if "op" in v:
        if v["op"] == "not":
            return omui.not_(args[0])
        if v["op"] == "neg":
            return -args[0]
        return BINARY[v["op"]](args[0], args[1])
    if "call" in v:
        return omui.call(v["call"], *args)
    if "fmt" in v:
        return omui.fmt(v["fmt"], *args)
    if "select" in v:
        c, a, b = [operand(x, row) for x in v["select"]]
        return omui.select(c, a, b)
    raise ValueError("an operand cannot be " + repr(v))


def element(e, row=None):
    if "when" in e:
        return omui.when(operand(e["when"], row), [element(c, row) for c in e.get("then", [])],
                         [element(c, row) for c in e.get("else", [])])
    if "each" in e:
        where = e.get("where")
        return omui.each(e["each"], lambda r: element(e["body"], r),
                         where=None if where is None else (lambda r: operand(where, r)))
    props = {}
    for k, v in e.get("props", {}).items():
        props[k] = operand(v, row)
    if "id" in e:
        props["id"] = e["id"]
    for event, actions in e.get("on", {}).items():
        props["on_" + event] = handler(actions)
    return getattr(omui, e["type"])(*[element(c, row) for c in e.get("children", [])], **props)


def handler(actions):
    def handle(event):
        for a in actions:
            action(a, event)
    return handle


# ── actions ───────────────────────────────────────────────────────────────

def value(v, event=None, args=None):
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
        return signals[v["get"]].get()
    out = {}
    for k, x in v.items():
        out[k] = value(x, event, args)
    return out


def native_function(name):
    """`fuzzy.rank` through the generated binding: omui.native.fuzzy.rank."""
    module, function = name.split(".")
    m = getattr(omui.native, module, None)
    return None if m is None else getattr(m, function, None)


def action(a, event=None, args=None):
    def val(v):
        return value(v, event, args)

    if "native" in a:
        signals[a["into"]].set(native_function(a["native"])(*[val(x) for x in a.get("args", [])]))
    elif "set" in a and "to" in a:
        signals[a["set"]].set(val(a["to"]))
    elif "add" in a:
        by, lo, hi = a["by"], a.get("min"), a.get("max")

        def bump(v):
            v += by
            v = v if lo is None else max(v, lo)
            return v if hi is None else min(v, hi)
        signals[a["add"]].update(bump)
    elif "invoke" in a:
        call = {}
        for k, x in a.get("args", {}).items():
            call[k] = val(x)
        omui.invoke(a["invoke"], call)
    elif "push" in a:
        row = {}
        for k, x in a["row"].items():
            row[k] = val(x)
        omui.model(a["push"]).push([row])
    elif "remove" in a or "change" in a:
        model = omui.model(a.get("remove") or a.get("change"))
        key = val(a["key"])
        rows = model.rows()
        found = [i for i in range(len(rows)) if rows[i]["key"] == key]
        if not found:
            raise ValueError("no row " + str(key))
        i = found[0]
        replace = []
        if "change" in a:
            row = rows[i]  # a copy already: rows() makes new values
            for k, x in a["set"].items():
                row[k] = val(x)
            replace = [row]
        model.splice(i, 1, replace)
    else:
        raise ValueError("an action cannot be " + repr(a))


# ── the scenario ──────────────────────────────────────────────────────────

def verb_of(d):
    def answer(args):
        for a in d.get("do", []):
            action(a, None, args)
        return value(d.get("result"), None, args)
    return answer


@omui.verb("cts.load", summary="Load a CTS scenario")
def load(args):
    s = args["scenario"]
    state["scenario"] = s
    for name in sorted(s.get("signals", {})):
        signals[name] = omui.signal(s["signals"][name])
    for name in sorted(s.get("models", {})):
        omui.model(name).push(s["models"][name])
    for name in sorted(s.get("verbs", {})):
        d = s["verbs"][name]
        omui.verb(name, verb_of(d), effect=d.get("effect", "reversible"), answer=d.get("answer", ""),
                  summary=d.get("summary", ""), person=d.get("person", False))
    omui.mount("main", element(s["view"]))
    return True


@omui.verb("cts.step", summary="Do a step of the loaded CTS scenario")
def step(args):
    i = args["step"]
    st = state["scenario"]["steps"][i]
    try:
        if "tree" in st:
            return {"step": i, "tree": omui.snapshot(root=st["tree"])["nodes"]}
        if "act" in st:
            request = {"node": st["act"], "action": st["action"]}
            if isinstance(st.get("args"), dict) and st["args"]:
                request["args"] = st["args"]
            if st.get("dry_run"):
                request["dry_run"] = True  # only what would happen (S-13)
            omui.act(request)  # the action's data, or OmuiError
            return {"step": i, "act": "ok"}
        if "invoke" in st:
            return {"step": i, "invoke": omui.invoke(st["invoke"], st.get("args", {}))}
        if "get" in st:
            return {"step": i, "get": signals[st["get"]].get()}
        if "rows" in st:
            return {"step": i, "rows": omui.model(st["rows"]).rows()}
        if "theme" in st:
            omui.theme(st["theme"])
            return {"step": i, "theme": "ok"}
        if "themes" in st:
            return {"step": i, "themes": omui.themes()}
        if "native" in st and "into" not in st:
            function = native_function(st["native"])
            if function is None:
                return {"step": i, "error": "not-found"}  # the binding has no such function
            return {"step": i, "native": function(*st.get("args", []))}
    except omui.OmuiError as e:
        return {"step": i, "error": str(e).split(":")[0]}
    action(st)
    return None
