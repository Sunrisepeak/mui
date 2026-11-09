# Component breakout — skill

How an agent plays the component breakout, headless, as a player. The operations and the tiers are the catalog's: [`breakout.ops.omui.ts`](breakout.ops.omui.ts) (listing [`media/ops/breakout.ops.json`](media/ops/breakout.ops.json), reference [`media/ops/breakout.ops.md`](media/ops/breakout.ops.md)).

## Before

1. The application must allow agent players: the field's `agent_player` is `human` or `assist` (`off` refuses `observe` and `play`). The person can turn it off; every call is admitted by the host's access (mlibs.control).
2. Know your tier. `human` is a person's: you are shown what is drawn, your actions take effect a tenth of a second after the step you saw, ten a second at most. `assist` is privileged and says so in the record.

## Playing

1. `observe` (a read): the step, the paddle, the balls (where, how fast, whether one waits on the paddle), score, lives, bricks left, whether the room is over.
2. `play` with `seen` = that step and what you hold: `axis` (−1 … 1, the arrows), `launch` (Space), `look` (Z). The answer says the step it takes effect; it holds until your next `play`.
3. Say why before you act (`intend`: where you mean the paddle to be, a short note): never played, it goes into the record and is drawn over the play when `agent_overlay` is on — a reviewer sees what you saw, what you meant and what you did.
4. Look often (it is a read), act at most at your tier's rate. A refusal says why: off, too often, or a step not yet played.
5. When the room is over, `next`.

## Verifying

- The field's `hash` (its semantic value) is the simulation's checksum: the same actions at the same steps give the same bytes; a game replays.
- What your tier showed is what you decided on: record observe's step with each play.
- Review: `record` exports the plays with their intent; `replay` plays a record alone from the room's start, and with `agent_overlay` its frames show the intent exactly as the live play did (the same pixels at the same step).
- Headless tests: `tests/agent_player_test.cpp` — an external agent through the protocol finishes a game at the human tier, twice to the same checksum; the tiers' differences; off refuses.

## Playtest (the agent as playtester)

`omui-breakout-playtest [--rooms 1,2,3,4,5] [--players 4] [--minutes 2] [--out DIR]` plays every room headless at a person's tier (the field's `human` tier, through the protocol), once per player of four skills (how often it looks, how far its aim is off: seeded), and writes `playtest.omui.json`: per room how often it was cleared, the median time, lives lost, the longest stretch with nothing scored, where the paddle missed (fifths of the width); each problem (out of lives, out of time, quiet over 30 s) with its record, replayed to the same bytes before it is reported. About 60× faster than real time: a whole run of the five rooms takes seconds. Read it as a designer: a room nobody clears, or misses piling up in one fifth, is a place to look — then open the record (`replay`) to see it happen.

