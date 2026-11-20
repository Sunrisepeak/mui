// Slow motion: the same game time saved, however slow.
import { verb } from "openmui";

const percent: number = 50; // ← tune: the clock's speed, 25 … 90 %
const saved: number = 4;    //   seconds of game time saved

verb("gift.slow", { effect: "read", summary: "What the slow-motion gift does" }, () => ({
  slow_percent: percent,
  slow_steps: Math.floor((saved / (1 - percent / 100)) * 120 + 0.5),
}));
