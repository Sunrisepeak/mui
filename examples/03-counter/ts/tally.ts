// The tally counter in TypeScript: the same tree as src/tally.cpp,
// ui/tally.view.omui.json and lua/tally.lua (the tests check it).
import { signal, fmt, call, select, ge, eq, column, row, text, progress, button, slider, mount } from "openmui";

const count = signal(0);
const goal = signal(10);

export const Tally = () =>
  column({ padding: 24, gap: 12 }, [
    text({ id: "count", text: call("string", count), style: "display" }),
    progress({
      id: "progress",
      label: "Towards the goal",
      value: call("clamp", call("double", count).div(call("double", call("max", goal, 1))), 0.0, 1.0),
    }),
    text({ id: "status", text: select(ge(count, goal), fmt("Goal of {} reached", goal), fmt("{} to go", goal.sub(count))) }),
    row({ id: "controls", gap: 8 }, [
      button({ id: "minus", text: "−1", disabled: eq(count, 0), onActivated: () => count.update((v: number) => Math.max(v - 1, 0)) }),
      button({ id: "plus", text: "+1", variant: "primary", onActivated: () => count.update((v: number) => v + 1) }),
      button({ id: "reset", text: "Reset", variant: "ghost", onActivated: () => count.set(0) }),
    ]),
    row({ id: "goal", gap: 8 }, [
      text({ text: "Goal" }),
      slider({ id: "value", label: "Goal", max: 100, min: 1, step: 1, value: goal }),
    ]),
  ]);

mount("main", Tally());
