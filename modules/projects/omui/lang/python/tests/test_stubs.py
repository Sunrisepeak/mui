"""The typing stubs (openmui._stubs) describe the package as it is: every
public name of ``__init__.py`` and every registered component, as valid
Python, the same text every time."""

import ast
import json
import unittest

import openmui as omui
from openmui import _openmui, _stubs


def top_level(tree):
    names = set()
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            names.add(node.name)
        elif isinstance(node, ast.Assign):
            names.update(t.id for t in node.targets if isinstance(t, ast.Name))
        elif isinstance(node, ast.ImportFrom):  # from . import native as native
            names.update(a.asname or a.name for a in node.names)
    return names


class Stubs(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads(_openmui.catalog())
        self.text = _stubs.stub(self.catalog)
        self.tree = ast.parse(self.text)

    def test_every_public_name_is_described(self):
        missing = set(omui.__all__) - top_level(self.tree)
        self.assertEqual(missing, set(), "names of __all__ the stubs leave out")

    def test_every_component_has_a_builder(self):
        names = top_level(self.tree)
        self.assertEqual(set(self.catalog["components"]) - names, set())
        builder = next(n for n in self.tree.body if isinstance(n, ast.FunctionDef) and n.name == "slider")
        params = {a.arg for a in builder.args.kwonlyargs}
        self.assertTrue({"id", "value", "min", "max", "on_changed", "padding"} <= params, sorted(params))
        self.assertIn("Chooses a number", ast.get_docstring(builder) or "")

    def test_the_stubs_are_the_same_every_time(self):
        self.assertEqual(_stubs.stub(json.loads(_openmui.catalog())), self.text)


if __name__ == "__main__":
    unittest.main()
