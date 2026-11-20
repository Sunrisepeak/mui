// More balls: ±20°, then ±40°.
import { verb } from "openmui";

const balls: number = 2; // ← tune: the extra balls from each flying one, 1 … 4

verb("gift.multi", { effect: "read", summary: "What the multi-ball gift does" }, () => ({ multi_balls: balls }));
