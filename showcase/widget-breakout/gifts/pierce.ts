// Piercing (TypeScript): the ball goes through bricks. The longer, the rarer.
import { verb } from "openmui";

const seconds: number = 5; // ← tune: how long it pierces, 1 … 20 seconds

verb("gift.pierce", { effect: "read", summary: "What the piercing gift does" }, () => ({
  pierce_steps: Math.round(seconds * 120),
  drop_one_in: Math.round(3 + seconds * 0.6), // a capsule one break in this many
}));
