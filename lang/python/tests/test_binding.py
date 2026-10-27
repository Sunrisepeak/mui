"""openmui in Python, as a script writes it: the counter of component-binding
§5.2 is the same tree as the document written by hand (CTS S level); signals,
expressions, loops, verbs; errors as exceptions; a headless run with pixels.

Run by src/entry/python's test with the built extension on PYTHONPATH."""

import json
import os
import tempfile
import unittest

import openmui as omui

COUNTER_DOCUMENT = {
    "omui": "0.1",
    "view": {
        "signals": {"count": {"type": "int", "value": 0}},
        "root": {"type": "column", "props": {"gap": 8}, "children": [
            {"type": "text", "props": {"text": {"expr": "format('{} clicks', count)"}}},
            {"type": "button", "id": "inc", "props": {"text": "+1"},
             "on": {"activated": [{"set": "count", "expr": "count + 1"}]}},
        ]},
    },
}


def nodes(slot):
    """The nodes under a slot, their ids made relative to it."""
    out = []
    for n in omui.snapshot(root=slot)["nodes"]:
        n = dict(n)
        n["id"] = n["id"][len(slot):]
        out.append(n)
    return out


def name_of(node_id):
    for n in omui.snapshot(root=node_id, depth=0)["nodes"]:
        if n["id"] == node_id:
            return n.get("name", "")
    return "<none>"


class Binding(unittest.TestCase):
    def test_counter_is_the_document_tree(self):
        count = omui.signal(0)

        def counter():
            with omui.column(gap=8):
                omui.text(text=omui.fmt("{} clicks", count))
                omui.button(id="inc", text="+1", on_activated=lambda e: count.update(lambda v: v + 1))

        omui.mount("py", counter)
        omui.mount_document("doc", COUNTER_DOCUMENT)
        omui.settle()
        self.assertEqual(nodes("py"), nodes("doc"))
        for _ in range(3):
            omui.act(node="py/inc", action="activate")
            omui.act(node="doc/inc", action="activate")
        omui.settle()
        self.assertEqual(name_of("py/text:0"), "3 clicks")
        self.assertEqual(nodes("py"), nodes("doc"))
        self.assertEqual(count.get(), 3)
        omui.unmount("py")
        omui.unmount("doc")

    def test_expressions_and_signals(self):
        a, b, name = omui.signal(6), omui.signal(7), omui.signal("ada")
        with omui.column() as root:
            omui.text(id="sum", text=omui.fmt("{}", a * b + 1))
            omui.text(id="who", text=omui.call("upper", name))
            omui.text(id="big", text=omui.select(a > 5, "big", "small"))
            omui.text(id="both", text=omui.select((a > 5) & ~(b == 0), "yes", "no"))
        omui.mount("e", root)
        omui.settle()
        self.assertEqual(name_of("e/sum"), "43")
        self.assertEqual(name_of("e/who"), "ADA")
        self.assertEqual(name_of("e/both"), "yes")
        seen = []
        a.watch(seen.append)
        a.set(1)
        self.assertEqual(a.get(), 1, "the mirror has it at once")
        omui.settle()
        self.assertEqual(name_of("e/big"), "small")
        self.assertEqual(seen, [1])
        with self.assertRaises(TypeError):
            bool(a)
        omui.unmount("e")

    def test_loops_verbs_and_regions(self):
        archived = []

        @omui.verb("note.archive", summary="Archive a note")
        def archive(args):
            archived.append(args["id"])
            return {"archived": args["id"]}

        only_open = omui.signal(False)
        notes = omui.model("notes")
        notes.push([{"key": "n1", "title": "Milk"}, {"key": "n2", "title": "Bread"}])

        def view():
            with omui.column():
                omui.when(only_open, omui.text(id="filter", text="open only"))
                omui.each("notes", lambda row: omui.row(
                    omui.text(text=row.title),
                    omui.button(id="archive", text="Archive",
                                on_activated=lambda e: omui.invoke("note.archive", {"id": e["rows"]["row"]["key"]})),
                ))

        omui.mount("l", view)
        omui.settle()
        self.assertEqual(name_of("l/n2/text:0"), "Bread")
        omui.act(node="l/n2/archive", action="activate")
        omui.settle()
        self.assertEqual(archived, ["n2"])
        only_open.set(True)
        omui.settle()
        self.assertEqual(name_of("l/filter"), "open only")
        self.assertEqual(omui.invoke("note.archive", {"id": "n1"}), {"archived": "n1"})
        omui.unmount("l")

    def test_errors_are_exceptions(self):
        with self.assertRaises(omui.OmuiError) as e:
            omui.mount("x", omui.element("buton", id="x"))
        self.assertIn("did you mean `button`", str(e.exception))
        with self.assertRaises(omui.OmuiError) as e:
            omui.invoke("no.such", {})
        self.assertIn("not-found", str(e.exception))
        with self.assertRaises(AttributeError):
            omui.no_such_component

    def test_the_view_document_of_a_python_view(self):
        count = omui.signal(0)
        doc = omui.document(lambda: omui.text(text=omui.fmt("{}", count)))
        self.assertEqual(doc["view"]["root"]["type"], "text")
        self.assertIn("provided", json.dumps(doc))

    def test_a_headless_run_writes_its_pixels(self):
        with tempfile.TemporaryDirectory() as d:
            png = os.path.join(d, "hello.png")
            self.assertEqual(omui.run(lambda: omui.text(text="hello"), size=(160, 80), png=png, argv=[]), 0)
            with open(png, "rb") as f:
                self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")
        omui.unmount("main")


if __name__ == "__main__":
    unittest.main()
