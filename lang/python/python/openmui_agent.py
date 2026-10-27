"""An agent's client for any running openmui application (openmui.tree@0.1).

Standard library only: it needs no part of openmui installed, so a script, a
test or a language model's tool can drive an application from any Python.

    import openmui_agent as agent

    app = agent.connect("org.openmui.wall")        # by its application id
    app.act("main/wall", "zoom_to", {"zoom": 1})
    keys = app.act("main/wall", "find", {"query": {"name": "Install"}})
    app.wait(settled=True, q={"id": "main/wall"})
    app.invoke("wall.stream", {"windows": 4, "rate": 50})

Every application listens at a name (its id) in a per-user directory: the
runtime directory ($XDG_RUNTIME_DIR, else the temporary one) plus "openmui",
beside a descriptor `<name>.endpoint.omui.json`. A request is one line of
JSON; the answer is records, one a line, the last a `result` with its exit
code (0 success, 1 failure, 2 needs an answer, 3 not ready). A failure
raises `AgentError` with the application's code and message — and the
answer it wants, for an irreversible action asked for without one.
"""

from __future__ import annotations

import json
import os
import socket
import sys
import tempfile
from pathlib import Path
from typing import Any, Optional

__all__ = ["Agent", "AgentError", "connect", "default_directory", "endpoints"]

INTERFACE = "openmui.tree@0.1"


class AgentError(RuntimeError):
    """A request the application refused or could not do."""

    def __init__(self, message: str, code: str = "", exit_code: int = 1, data: Any = None) -> None:
        super().__init__(message)
        self.code = code
        self.exit_code = exit_code
        self.data = data


def default_directory() -> Path:
    """Where applications listen: as openmui's runtime chooses (agent::default_directory)."""
    base = os.environ.get("XDG_RUNTIME_DIR")
    return (Path(base) if base else Path(tempfile.gettempdir())) / "openmui"


def endpoints(directory: Optional[Path] = None) -> list[dict[str, Any]]:
    """The applications described in a directory (running or left behind: connect to know)."""
    d = Path(directory) if directory else default_directory()
    out = []
    for f in sorted(d.glob("*.endpoint.omui.json")):
        try:
            out.append(json.loads(f.read_text(encoding="utf-8")))
        except (OSError, ValueError):
            continue
    return out


class _Unix:
    """A stream socket at a path: AF_UNIX where Python has it; on Windows,
    whose Python lacks it though Windows has it, through Winsock."""

    def __init__(self, path: str) -> None:
        if hasattr(socket, "AF_UNIX"):
            self._s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self._s.connect(path)
            self._w = None
        else:  # pragma: no cover - Windows
            self._s = None
            self._w = _Winsock(path)

    def send(self, data: bytes) -> None:
        if self._s is not None:
            self._s.sendall(data)
        else:
            self._w.send(data)

    def recv(self, n: int) -> bytes:
        return self._s.recv(n) if self._s is not None else self._w.recv(n)

    def close(self) -> None:
        if self._s is not None:
            self._s.close()
        else:
            self._w.close()


class _Winsock:  # pragma: no cover - Windows
    """AF_UNIX on Windows 10 1803 and later, by ws2_32 (as openkal reaches it)."""

    def __init__(self, path: str) -> None:
        import ctypes
        from ctypes import wintypes

        self._ws = ctypes.WinDLL("ws2_32", use_last_error=True)
        data = ctypes.create_string_buffer(408)
        if self._ws.WSAStartup(0x0202, data) != 0:
            raise OSError("WSAStartup failed")
        self._ws.socket.restype = ctypes.c_size_t
        self._sock = self._ws.socket(1, 1, 0)  # AF_UNIX, SOCK_STREAM
        if self._sock == ctypes.c_size_t(-1).value:
            raise OSError(f"no AF_UNIX socket: {ctypes.get_last_error()}")
        raw = os.fsencode(path)
        if len(raw) >= 108:
            raise OSError("the endpoint's path is too long for a socket")

        class SockaddrUn(ctypes.Structure):
            _fields_ = [("family", wintypes.USHORT), ("path", ctypes.c_char * 108)]

        addr = SockaddrUn(1, raw)
        if self._ws.connect(ctypes.c_size_t(self._sock), ctypes.byref(addr), ctypes.sizeof(addr)) != 0:
            err = self._ws.WSAGetLastError()
            self.close()
            raise ConnectionRefusedError(f"no application answers at {path} ({err})")
        self._ctypes = ctypes

    def send(self, data: bytes) -> None:
        view = memoryview(data)
        while view:
            n = self._ws.send(self._ctypes.c_size_t(self._sock), bytes(view), len(view), 0)
            if n <= 0:
                raise ConnectionError("the application closed the connection")
            view = view[n:]

    def recv(self, n: int) -> bytes:
        buf = self._ctypes.create_string_buffer(n)
        got = self._ws.recv(self._ctypes.c_size_t(self._sock), buf, n, 0)
        if got < 0:
            raise ConnectionError("reading from the application failed")
        return buf.raw[:got]

    def close(self) -> None:
        self._ws.closesocket(self._ctypes.c_size_t(self._sock))
        self._ws.WSACleanup()


class Agent:
    """A connection to one application; requests are answered in order."""

    def __init__(self, name: str, directory: Optional[Path] = None, actor: str = "agent:python") -> None:
        self.name = name
        self.directory = Path(directory) if directory else default_directory()
        self.actor = actor
        self._conn = _Unix(str(self.directory / name))
        self._buffer = b""

    def close(self) -> None:
        self._conn.close()

    def __enter__(self) -> "Agent":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    # ── the protocol ──
    def request(self, op: str, **fields: Any) -> Any:
        """One request: the `data` it answered with, or AgentError."""
        req = {"op": op, **{k: v for k, v in fields.items() if v is not None}}
        self._conn.send((json.dumps(req, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8"))
        data: Any = None
        error: Optional[dict[str, Any]] = None
        while True:
            record = self._line()
            kind = record.get("kind")
            if kind == "data":
                data = record.get("data")
            elif kind == "error":
                error = record
            elif kind == "result":
                code = int(record.get("exitCode", 1))
                if code == 0:
                    return data
                message = error.get("message", "") if error else f"{op} failed"
                raise AgentError(message, error.get("code", "") if error else "", code, error.get("data") if error else None)

    def _line(self) -> dict[str, Any]:
        while b"\n" not in self._buffer:
            chunk = self._conn.recv(65536)
            if not chunk:
                raise ConnectionError(f"{self.name} closed the connection")
            self._buffer += chunk
        line, self._buffer = self._buffer.split(b"\n", 1)
        return json.loads(line.decode("utf-8"))

    def describe(self) -> dict[str, Any]:
        return self.request("describe")

    def snapshot(self, root: Optional[str] = None, interactive: Optional[bool] = None, depth: Optional[int] = None) -> list[dict[str, Any]]:
        return self.request("snapshot", root=root, interactive=interactive, depth=depth)["nodes"]

    def find(self, **q: Any) -> list[dict[str, Any]]:
        return self.request("find", q=q)["nodes"]

    def act(self, node: str, action: str, args: Any = None, answer: Optional[str] = None) -> Any:
        return self.request("act", node=node, action=action, args=args, answer=answer, actor=self.actor)

    def invoke(self, verb: str, args: Any = None, answer: Optional[str] = None) -> Any:
        return self.request("invoke", verb=verb, args=args, answer=answer, actor=self.actor)

    def wait(self, q: Optional[dict[str, Any]] = None, absent: Optional[bool] = None, settled: Optional[bool] = None,
             timeout: Optional[float] = None) -> Any:
        return self.request("wait", q=q, absent=absent, settled=settled, timeout=timeout)


def connect(name: str, directory: Optional[Path] = None, actor: str = "agent:python") -> Agent:
    """The application listening as `name` (its id)."""
    return Agent(name, directory, actor)


if __name__ == "__main__":  # `python -m openmui_agent`: what runs
    for e in endpoints(Path(sys.argv[1]) if len(sys.argv) > 1 else None):
        print(f"{e.get('name', '?')}\t{e.get('app', '')}")
