# gongfu

The fighting framework's core (D-85): a match between two fighters, one frame (1/60 s) at a time, in fixed-point numbers, with rollback. It depends on the standard library alone; drawing (2D now, 3D later), sound, devices and the network read its state and its events from outside.

```cpp
import gongfu;

auto fighter = gongfu::compile(definition);   // every problem named with its move and field
gongfu::Match match(gongfu::SystemDef{}, *fighter, *fighter);
match.tick(gongfu::pad(6, gongfu::bit(gongfu::Button::lp)), gongfu::pad(5));
const gongfu::State saved = match.state();    // a rollback's save: one copy
match.restore(saved);                         // and its load
for (const gongfu::Event& e : match.events()) { /* hits, blocks, throws, KO: what draws and sounds read */ }
```

- **Numbers** (`:fixed`): Q16.16 in 32 bits, products floored and quotients truncated through 64; literals (`2.5_fx`) converted at compile time. No float in the simulation.
- **Input** (`:input`): pads (four directions, six buttons), a 64-frame log of directions relative to the fighter's facing, buttons held and pressed; SOCD resolved (neutral, last wins, up wins); commands — motions in a window (236, 214, 623, 63214), charges (back or down held, then the other way), a direction held at the press, buttons any or all together, a buffer. The log lives in the state, so a rollback restores what the inputs were.
- **Data** (`:data`): a fighter's body and moves — frames, hits (boxes, damage, chip, hitstun, blockstun, hitstop, pushback, level, knockdown, launch, the impact profile drawing reads), cancels on contact, invulnerability, movement steps, throws, projectiles, meter, a super's freeze — and the system's rules (combo scaling, counter hits, the round, the stage, proximity guard, SOCD). `compile` checks a fighter and resolves its cancels.
- **The match** (`:match`): both fighters decide from the frame's start, move, are pushed apart and kept on the stage; strikes, throws and projectiles are found against each other before any is applied, so two on one frame trade. Blocking: mid either way, low crouching, overhead standing; holding back near a move guards in place. Throws break when both throw or the defender presses LP+LK in time. A cancel pressed during a hitstop starts when it ends.
- **The state** (`:state`): one block of plain data without padding (checked at compile time); `checksum` is the hash of its bytes.

`tests/gongfu_test.cpp` holds the numbers, the input reading, the data's problems, movement, frame data measured as declared (startup, active, recovery on whiff; advantage on hit and block = stun − what the attacker has left), blocking, cancels and combo scaling, counter hits, throws and breaks, projectiles and clashes, invulnerability — and 10,000 rollbacks of random depth over 10,000 random frames leaving every frame's state the bytes of a run without them. The run's hash is pinned: GCC, LLVM and wasm32-emscripten play those inputs into the same bytes.
