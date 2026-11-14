# openmui_agent — an agent's client in Python

A client for any running openmui application over its agent protocol (openmui.tree@0.1), in one file, the standard library only: a script, a test or a language model's tool drives an application from any Python 3.10+.

```python
import openmui_agent as agent          # PYTHONPATH=modules/projects/omui/agent/clients/python
app = agent.connect("org.openmui.wall")
app.act("main/wall", "zoom_to", {"zoom": 1})
app.invoke("wall.stream", {"windows": 4, "rate": 50})
```

It is a client of the protocol, not a way to write an application: applications are written in TypeScript (and C++), D-138. The markdown wall's tour (`showcase/markdown-wall/scripts/tour.py`) is driven with it.
