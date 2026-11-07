"""A tour of the wall through the semantic API — the same in tour.ts and
tour.lua (the tests check they record the same and leave the same wall).
Each step's result is recorded with the verb `tour.record`. Python drives
the wall as an agent does, through its endpoint:

    python tour.py NAME [DIRECTORY]
"""

import sys
from typing import Any, Callable

import openmui_agent as agent

app = agent.connect(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None, actor="agent:tour")
WALL = "main/wall"


def record(step: str, value: Any) -> None:
    app.invoke("tour.record", {"step": step, "value": value})


def refusal(f: Callable[[], Any]) -> str:
    try:
        f()
        return "done"
    except agent.AgentError as e:
        return e.code


# Find a document by name, anywhere on the wall; how many workspaces.
using = app.act(WALL, "find", {"query": {"name": "using"}})
record("using", using)
record("workspaces", len(app.act(WALL, "find", {"query": {"kind": "workspace"}})))
# Fly to it (it goes live), move it, choose it.
record("focus", app.act(WALL, "focus_node", {"key": using[0]}))
record("move", app.act(f"{WALL}/{using[0]}", "move_to", {"x": 4000, "y": 3000}))
record("select", app.act(f"{WALL}/{using[0]}", "select"))
# What is near where it went, nearest first.
record("near", app.act(WALL, "find", {"query": {"near": [4000, 3000], "radius": 900}}))
# Two windows written into, then not.
record("stream", app.invoke("wall.stream", {"windows": 2, "rate": 50}))
record("stop", app.invoke("wall.stop", {}))
# What the wall refuses: a window has no `delete` here.
record("refused", refusal(lambda: app.act(f"{WALL}/{using[0]}", "delete", answer="yes")))
# Back to the whole wall.
record("overview", app.act(WALL, "camera_preset", {"name": "overview"}))
app.close()
