// The film's tally counter in TypeScript (after example 03), as the code
// shot types it and runs it: the same tree as tally.cpp, tally.py and
// tally.lua beside it.
import { signal, fmt, call, select, ge, eq, mount } from "openmui";
import { column, row, text, progress, button } from "openmui";

const count = signal(0);
const goal = signal(10);
const share = call("double", count).div(call("double", call("max", goal, 1)));

mount("main", column({ padding: 24, gap: 12 }, [
  text({ id: "count", text: call("string", count), style: "display" }),
  progress({ id: "progress", label: "Towards the goal",
             value: call("clamp", share, 0.0, 1.0) }),
  text({ id: "status", text: select(ge(count, goal),
         fmt("Goal of {} reached", goal),
         fmt("{} to go", goal.sub(count))) }),
  row({ id: "controls", gap: 8 }, [
    button({ id: "minus", text: "−1", disabled: eq(count, 0),
             onActivated: () => count.update((v: number) =>
               Math.max(v - 1, 0)) }),
    button({ id: "plus", text: "+1", variant: "primary",
             onActivated: () => count.update((v: number) => v + 1) }),
    button({ id: "reset", text: "Reset", variant: "ghost",
             onActivated: () => count.set(0) }),
  ]),
]));
