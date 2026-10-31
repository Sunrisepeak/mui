# gongfu.ai

A computer opponent for gongfu, and fighters measured against each other: the report the roster's balance is tuned by ([characters 2 design](../../../.agents/docs/2026-10-28-gongfu-characters-2-design.md) §7).

```cpp
import gongfu.ai;

const gongfu::ai::Kit kit = gongfu::ai::read_kit(fighter);  // what its moves are for, from their data
gongfu::ai::Bot bot(fighter, 1, /*seed*/ 7);                  // plays the right side
match.tick(player_pad, bot.next(match));
const auto record = gongfu::ai::versus(system, a, b, 200, /*seed*/ 1);  // wins, damage, round length
```

- **The kit** (`read_kit`): a fighter's moves sorted by what they are for, from their data alone:
  - pokes by reach;
  - anti-airs (invulnerable to the air early);
  - projectiles;
  - throws and command throws;
  - air throws;
  - jump-ins;
  - dashes;
  - set-ups (panes, marks);
  - supers by level.
- **The bot** (`Bot`):
  - It sees the other side `reaction` frames late (14 by default, about a person's).
  - It guards a strike it saw coming (low against a low), answers a jump with an anti-air, punishes a whiff in reach, and confirms a hit into a special.
  - By distance it zones, pokes, walks in, jumps in, throws, and spends meter on a super in range.
  - Every choice comes from its own seeded generator: a round plays the same every time, on every machine.
- **Rounds** (`play`, `versus`): a round between two bots, and a pair's record over many with the sides swapped every other round. It counts wins, draws, damage dealt each way, round length and supers.

`gongfu-studio versus <directory>` prints every pair's record as JSON (in [studio](../studio)). The fighting game's tests keep the roster within its balance gates.
