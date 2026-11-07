"""The film's tally counter in Python (after example 03), as the code shot
shows it and runs it, embedded: the same tree as tally.ts, tally.cpp and
tally.lua beside it."""
import openmui as omui

count = omui.signal(0)
goal = omui.signal(10)
share = omui.call("double", count) / omui.call(
    "double", omui.call("max", goal, 1))


def tally() -> None:
    with omui.column(padding=24, gap=12):
        omui.text(id="count", text=omui.call("string", count), style="display")
        omui.progress(id="progress", label="Towards the goal",
                      value=omui.call("clamp", share, 0.0, 1.0))
        omui.text(id="status", text=omui.select(count >= goal,
                  omui.fmt("Goal of {} reached", goal),
                  omui.fmt("{} to go", goal - count)))
        with omui.row(id="controls", gap=8):
            omui.button(id="minus", text="−1", disabled=count == 0,
                        on_activated=lambda e: count.update(
                            lambda v: max(v - 1, 0)))
            omui.button(id="plus", text="+1", variant="primary",
                        on_activated=lambda e: count.update(lambda v: v + 1))
            omui.button(id="reset", text="Reset", variant="ghost",
                        on_activated=lambda e: count.set(0))


if __name__ == "__main__":
    raise SystemExit(omui.run(tally, title="Tally", size=(360, 240)))
