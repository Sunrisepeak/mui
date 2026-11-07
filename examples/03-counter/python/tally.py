"""The tally counter in Python: the same tree as src/tally.cpp,
ui/tally.view.omui.json, lua/tally.lua and ts/tally.ts (the tests check it).

    python python/tally.py              a window
    python python/tally.py --headless --png tally.png
"""

import openmui as omui

count = omui.signal(0)
goal = omui.signal(10)


def tally() -> None:
    with omui.column(padding=24, gap=12):
        omui.text(id="count", text=omui.call("string", count), style="display")
        omui.progress(id="progress", label="Towards the goal",
                      value=omui.call("clamp", omui.call("double", count) / omui.call("double", omui.call("max", goal, 1)), 0.0, 1.0))
        omui.text(id="status", text=omui.select(count >= goal, omui.fmt("Goal of {} reached", goal), omui.fmt("{} to go", goal - count)))
        with omui.row(id="controls", gap=8):
            omui.button(id="minus", text="−1", disabled=count == 0,
                        on_activated=lambda e: count.update(lambda v: max(v - 1, 0)))
            omui.button(id="plus", text="+1", variant="primary", on_activated=lambda e: count.update(lambda v: v + 1))
            omui.button(id="reset", text="Reset", variant="ghost", on_activated=lambda e: count.set(0))
        with omui.row(id="goal", gap=8):
            omui.text(text="Goal")
            omui.slider(id="value", label="Goal", max=100, min=1, step=1, value=goal)


if __name__ == "__main__":
    raise SystemExit(omui.run(tally, title="Tally", size=(360, 300)))
