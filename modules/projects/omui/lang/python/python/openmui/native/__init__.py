"""Native modules (component-binding §8): functions written in C++ behind a
contract, made from the program's catalogue.

    from openmui.native import fuzzy
    best = fuzzy.rank("mcfg", files, 20)        # positional, as the contract orders them
    best = fuzzy.rank(query="mcfg", candidates=files)

A call runs on the caller's thread (async functions too: Python's thread is
not the runtime's). Errors are omui.OmuiError, the contract's checks included.
The typing stubs of the official modules are beside this file (fuzzy.pyi).
"""

import json as _json

from .. import _openmui as _n
from .. import OmuiError

_modules = {}


def _catalog():
    return _json.loads(_n.natives())["modules"]


class _Module:
    """A native module: its functions as attributes."""

    def __init__(self, name, spec):
        self.__name__ = name
        self.__doc__ = spec.get("doc", "")
        self._functions = spec["functions"]

    def __getattr__(self, function):
        spec = self._functions.get(function)
        if spec is None:
            raise AttributeError(f"the native module `{self.__name__}` has no function `{function}` (it has {', '.join(self._functions)})")
        module = self.__name__

        def call(*args, **kwargs):
            if args and kwargs:
                raise TypeError(f"{module}.{function}: arguments by position or by name, not both")
            payload = _json.dumps(list(args) if not kwargs else kwargs, ensure_ascii=False)
            try:
                return _json.loads(_n.native(module, function, payload))
            except RuntimeError as e:
                raise OmuiError(str(e)) from None

        call.__name__ = function
        call.__qualname__ = f"{module}.{function}"
        call.__doc__ = spec.get("doc", "")
        setattr(self, function, call)
        return call

    def __dir__(self):
        return sorted(self._functions)

    def __repr__(self):
        return f"<openmui native module {self.__name__}>"


def __getattr__(name):
    if name.startswith("_"):
        raise AttributeError(name)
    if name not in _modules:
        spec = _catalog().get(name)
        if spec is None:
            raise AttributeError(f"no native module is named `{name}` (the program registers its native modules before scripts run)")
        _modules[name] = _Module(name, spec)
    return _modules[name]


def __dir__():
    return sorted(_catalog())
