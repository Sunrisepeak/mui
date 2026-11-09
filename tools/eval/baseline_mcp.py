#!/usr/bin/env python3
"""The control arms of the observation experiment (D-142, W4): an MCP server
that offers an agent what other ways of operating an application offer —
on the same running application, so only the interface differs.

  baseline_mcp.py --omui OMUI --dir ENDPOINTS --way screenshot|a11y-tree [--app NAME] [--actor KIND:NAME]

  screenshot  what computer use gives: a picture of the window, and a
              person's devices at coordinates — click, double_click, drag,
              type, key, scroll. Nothing else about the application.
  a11y-tree   what a browser agent's accessibility tree gives (Playwright
              MCP's snapshot): each node's role, name, value, states and
              bounds with a ref, and the same devices, by ref or at
              coordinates — and the picture. No operations of the
              application (no actions by name, verbs, previews, undo).

Everything goes through the application's agent protocol as `omui agent`
does: pictures are `capture`, devices are `input` (decided as an agent's
input is, openas §2.7), the tree is `snapshot` with the semantics a platform
accessibility tree lacks taken out. tasks.py runs a task set through either
arm (`--via screenshot`, `--via a11y-tree`) and checks the outcome on the
application's state as it does for the semantic arm.
"""
from __future__ import annotations

import argparse
import base64
import json
import subprocess
import sys
import tempfile
from pathlib import Path

SCREENSHOT_TOOLS = [
    {"name": "screenshot", "description": "A picture of the application's window (PNG).", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "click", "description": "Clicks at a point of the window, in its pixels.",
     "inputSchema": {"type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"},
                                                      "button": {"type": "string", "enum": ["left", "right", "middle"]}}, "required": ["x", "y"]}},
    {"name": "double_click", "description": "Clicks twice at a point of the window.",
     "inputSchema": {"type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}}, "required": ["x", "y"]}},
    {"name": "drag", "description": "Presses at one point, moves to another, releases.",
     "inputSchema": {"type": "object", "properties": {"from_x": {"type": "number"}, "from_y": {"type": "number"}, "to_x": {"type": "number"},
                                                      "to_y": {"type": "number"}}, "required": ["from_x", "from_y", "to_x", "to_y"]}},
    {"name": "type", "description": "Types text where the keyboard focus is.",
     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]}},
    {"name": "key", "description": "Presses a key (Enter, Escape, Tab, Backspace, ArrowDown, Ctrl+A …).",
     "inputSchema": {"type": "object", "properties": {"key": {"type": "string"}}, "required": ["key"]}},
    {"name": "scroll", "description": "Turns the wheel at a point (dy < 0 scrolls up).",
     "inputSchema": {"type": "object", "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "dy": {"type": "number"}},
                     "required": ["x", "y", "dy"]}},
]

A11Y_TOOLS = [
    {"name": "accessibility_tree",
     "description": "The window's accessibility tree: each node's ref, role, name, value, states and bounds (x, y, width, height), nested.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "click_ref", "description": "Clicks the middle of the node with this ref.",
     "inputSchema": {"type": "object", "properties": {"ref": {"type": "string"}}, "required": ["ref"]}},
]

# What a platform's accessibility tree carries of a node; the rest (its actions
# and their effects, the application's verbs) is what the semantic arm adds.
KEPT = ("role", "name", "value", "states", "bounds")


class Application:
    def __init__(self, omui: str, endpoints: str, app: str | None, actor: str):
        self.omui, self.endpoints, self.actor = omui, endpoints, actor
        self.app = app or self.only_application()

    def only_application(self) -> str:
        # `omui agent ls` is a table: name, id, protocol — a line each.
        done = subprocess.run([self.omui, "--dir", self.endpoints, "agent", "ls"], capture_output=True, text=True, timeout=60)
        names = [line.split("\t")[0] for line in done.stdout.splitlines() if line.strip()]
        if not names:
            raise SystemExit("baseline_mcp: no application listens (omui agent ls)")
        return names[0]

    def agent(self, *args: str) -> list[dict]:
        done = subprocess.run([self.omui, "--dir", self.endpoints, "agent", *args], capture_output=True, text=True, timeout=120)
        records = []
        for line in done.stdout.splitlines():
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                continue
        return records

    def call(self, request: dict) -> list[dict]:
        return self.agent("call", self.app, json.dumps({**request, "actor": self.actor}))

    def picture(self) -> str:
        with tempfile.TemporaryDirectory(prefix="omui-baseline-") as scratch:
            out = Path(scratch) / "window.png"
            self.agent("capture", self.app, "--out", str(out))
            return base64.b64encode(out.read_bytes()).decode("ascii") if out.exists() else ""

    def input(self, events: list[dict]) -> str:
        records = self.call({"op": "input", "events": events})
        errors = [r.get("text", "") for r in records if r.get("kind") == "error"]
        return "; ".join(errors) if errors else "done"

    def tree(self) -> list[dict]:
        nodes = []
        for record in self.agent("snapshot", self.app):
            data = record.get("data") if isinstance(record, dict) else None
            if isinstance(data, dict) and isinstance(data.get("nodes"), list):
                nodes = data["nodes"]
        by_id = {n.get("id"): n for n in nodes}
        children_of = {n.get("id"): n.get("children", []) for n in nodes}
        seen: set[str] = set()

        def kept(node_id: str) -> dict:
            seen.add(node_id)
            node = by_id.get(node_id, {})
            out = {"ref": node_id, **{k: node[k] for k in KEPT if k in node and node[k] not in (None, "", [], {})}}
            kids = [kept(c) for c in children_of.get(node_id, []) if c in by_id]
            if kids:
                out["children"] = kids
            return out

        roots = [n.get("id") for n in nodes if not any(n.get("id") in children_of.get(other, []) for other in children_of)]
        return [kept(r) for r in roots if r not in seen]


def run_tool(application: Application, name: str, args: dict) -> list[dict]:
    if name == "screenshot":
        data = application.picture()
        return [{"type": "image", "data": data, "mimeType": "image/png"}] if data else [{"type": "text", "text": "no picture"}]
    if name == "accessibility_tree":
        return [{"type": "text", "text": json.dumps(application.tree(), ensure_ascii=False)}]
    events = {
        "click": lambda: [{"type": "click", "x": args["x"], "y": args["y"], **({"button": args["button"]} if args.get("button") else {})}],
        "double_click": lambda: [{"type": "click", "x": args["x"], "y": args["y"]}, {"type": "click", "x": args["x"], "y": args["y"]}],
        "drag": lambda: [{"type": "drag", "from": [args["from_x"], args["from_y"]], "to": [args["to_x"], args["to_y"]]}],
        "type": lambda: [{"type": "text", "text": args["text"]}],
        "key": lambda: [{"type": "key", "key": args["key"]}],
        "scroll": lambda: [{"type": "wheel", "x": args["x"], "y": args["y"], "dy": args["dy"]}],
        "click_ref": lambda: [{"type": "click", "node": args["ref"]}],
    }.get(name)
    if not events:
        return [{"type": "text", "text": f"no tool {name}"}]
    return [{"type": "text", "text": application.input(events() + [{"type": "wait", "frames": 2}])}]


def serve(application: Application, tools: list[dict]) -> None:
    names = {t["name"] for t in tools}
    for line in sys.stdin:
        try:
            request = json.loads(line)
        except json.JSONDecodeError:
            continue
        method, ident = request.get("method"), request.get("id")
        if ident is None:  # a notification
            continue
        if method == "initialize":
            result = {"protocolVersion": request.get("params", {}).get("protocolVersion", "2025-06-18"), "capabilities": {"tools": {}},
                      "serverInfo": {"name": "omui-baseline", "version": "0.1.0"}}
        elif method == "tools/list":
            result = {"tools": tools}
        elif method == "tools/call":
            params = request.get("params", {})
            name = params.get("name", "")
            if name not in names:
                result = {"content": [{"type": "text", "text": f"no tool {name}"}], "isError": True}
            else:
                try:
                    result = {"content": run_tool(application, name, params.get("arguments") or {})}
                except (KeyError, subprocess.TimeoutExpired) as error:
                    result = {"content": [{"type": "text", "text": f"{name}: {error}"}], "isError": True}
        elif method == "ping":
            result = {}
        else:
            sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": ident, "error": {"code": -32601, "message": f"no method {method}"}}) + "\n")
            sys.stdout.flush()
            continue
        sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": ident, "result": result}) + "\n")
        sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--omui", required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--way", required=True, choices=("screenshot", "a11y-tree"))
    parser.add_argument("--app")
    parser.add_argument("--actor", default="agent:baseline")
    arguments = parser.parse_args()
    application = Application(arguments.omui, arguments.dir, arguments.app, arguments.actor)
    serve(application, SCREENSHOT_TOOLS + (A11Y_TOOLS if arguments.way == "a11y-tree" else []))
    return 0


if __name__ == "__main__":
    sys.exit(main())
