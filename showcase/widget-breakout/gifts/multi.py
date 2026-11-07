# More balls (Python: PocketPy, a supported subset): ±20°, then ±40°.
import openmui as omui
balls = 2  # ← tune: the extra balls from each flying one, 1 … 4


@omui.verb("gift.multi", effect="read", summary="What the multi-ball gift does")
def rules(args):
    return {"multi_balls": balls}
