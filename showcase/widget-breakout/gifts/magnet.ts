// The magnet: the paddle holds a returned ball, and you aim again.
import { verb } from "openmui";

const catches: number = 3; // ← tune: the returns it holds, 1 … 8

verb("gift.magnet", { effect: "read", summary: "What the magnet does" }, () => ({ magnet_catches: catches }));
