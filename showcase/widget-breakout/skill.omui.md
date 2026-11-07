# Component breakout — skill

How an agent plays the component breakout, headless, as a player. The operations and the tiers are the catalog's: [`breakout.ops.omui.toml`](breakout.ops.omui.toml) (listing [`media/ops/breakout.ops.json`](media/ops/breakout.ops.json), reference [`media/ops/breakout.ops.md`](media/ops/breakout.ops.md)).

## Before

1. The application must allow agent players: the field's `agent_player` is `human` or `assist` (`off` refuses `observe` and `play`). The person can turn it off; every call is admitted by the host's access (mlibs.control).
2. Know your tier. `human` is a person's: you are shown what is drawn, your actions take effect a tenth of a second after the step you saw, ten a second at most. `assist` is privileged and says so in the record.

## Playing

1. `observe` (a read): the step, the paddle, the balls (where, how fast, whether one waits on the paddle), score, lives, bricks left, whether the room is over.
2. `play` with `seen` = that step and what you hold: `axis` (−1 … 1, the arrows), `launch` (Space), `look` (Z). The answer says the step it takes effect; it holds until your next `play`.
3. Look often (it is a read), act at most at your tier's rate. A refusal says why: off, too often, or a step not yet played.
4. When the room is over, `next`.

## Verifying

- The field's `hash` (its semantic value) is the simulation's checksum: the same actions at the same steps give the same bytes; a game replays.
- What your tier showed is what you decided on: record observe's step with each play.
- Headless tests: `tests/agent_player_test.cpp` — an external agent through the protocol finishes a game at the human tier, twice to the same checksum; the tiers' differences; off refuses.
