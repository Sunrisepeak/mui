"""Example 03 in Python (examples/03-counter/python/tally.py) is the same
semantic tree and the same pixels as its view document, before and after
every operation (CTS S level), and the pixels the example's test pins. That
test holds the document to the C++, Lua and TypeScript versions, so the five
agree."""

import importlib.util
import os
import tempfile
import unittest

import openmui as omui

EXAMPLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "examples", "03-counter")


def load_tally():
    spec = importlib.util.spec_from_file_location("tally", os.path.join(EXAMPLE, "python", "tally.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def state(png_dir, when):
    """The tree under `main` and the pixels of the frame, after an operation."""
    omui.settle()
    png = os.path.join(png_dir, when + ".png")
    omui.png(png)
    with open(png, "rb") as f:
        pixels = f.read()
    return omui.snapshot(root="main")["nodes"], pixels


def act(node, action, args=None):
    answer = omui.act(node=node, action=action, args=args or {})
    omui.settle()
    return answer


# What the example's test does to both versions, in its order.
OPERATIONS = [
    ("plus", lambda: act("main/controls/plus", "activate")),
    ("plus twice", lambda: act("main/controls/plus", "activate")),
    ("minus", lambda: act("main/controls/minus", "activate")),
    ("goal of 1", lambda: act("main/goal/value", "set_value", {"value": 1})),
    ("reset", lambda: act("main/controls/reset", "activate")),
]


class ExampleCounter(unittest.TestCase):
    def run_version(self, mount, png_dir):
        mount()
        out = [("mounted", *state(png_dir, "mounted"))]
        for when, operation in OPERATIONS:
            operation()
            out.append((when, *state(png_dir, when)))
        omui.unmount("main")
        return out

    def test_the_python_version_is_the_document(self):
        omui.run(None, size=(360, 300), headless=True, argv=[])
        tally = load_tally()
        with tempfile.TemporaryDirectory() as d:
            python = self.run_version(lambda: omui.mount("main", tally.tally), d)
            document = self.run_version(
                lambda: omui.mount_document("main", os.path.join(EXAMPLE, "ui", "tally.view.omui.json")), d)
        self.assertEqual(len(python), len(document))
        for (when, nodes, pixels), (_, doc_nodes, doc_pixels) in zip(python, document):
            with self.subTest(when=when):
                self.assertEqual(nodes, doc_nodes)
                self.assertEqual(pixels, doc_pixels, "the pixels")
        # What the operations did, read from the tree the user sees.
        names = {when: {n["id"]: n.get("name", "") for n in nodes} for when, nodes, _ in python}
        self.assertEqual(names["plus twice"]["main/count"], "2")
        self.assertEqual(names["plus twice"]["main/status"], "8 to go")
        self.assertEqual(names["goal of 1"]["main/status"], "Goal of 1 reached")
        self.assertEqual(names["reset"]["main/count"], "0")

    def test_the_pixels_are_the_examples(self):
        # As examples/03-counter/tests/counter_test.cpp pins them: a count of 7.
        omui.run(None, size=(360, 300), headless=True, argv=[])
        tally = load_tally()
        tally.count.set(7)
        tally.goal.set(10)
        omui.mount("main", tally.tally)
        omui.settle()
        self.assertEqual(omui.pixels(), "2ca1490961252cf6")
        omui.unmount("main")


if __name__ == "__main__":
    unittest.main()
