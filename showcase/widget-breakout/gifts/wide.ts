// The wide paddle (TypeScript). Its power is fixed: the wider, the shorter.
import { verb } from "openmui";

const half: number = 110;   // ← tune: half the paddle's width, 80 … 300 pixels
const power: number = 1100; //   half-width × seconds, whatever you choose

const seconds: number = power / half;
verb("gift.wide", { effect: "read", summary: "What the wide-paddle gift does" }, () => ({
  wide_half: half,
  wide_steps: Math.round(seconds * 120),
}));
