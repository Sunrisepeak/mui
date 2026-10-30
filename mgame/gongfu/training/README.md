# gongfu.training

What training mode shows, measured (D-85 §8.5). Everything is read from the match after each frame: its state, its events, and whether each fighter has recovered. So the numbers hold for any fighter's data, and nothing is allocated per frame (fixed rings).

```cpp
gongfu::training::Readout readout;
match.tick(p1, p2);
readout.record(match, p1, p2);
readout.meter(0);       // the last 80 frames' phases, oldest first
readout.advantage();    // +3: the attacker acts three frames first
readout.combo();        // {attacker, hits, damage, going}
readout.inputs(0);      // the pads held, and for how many frames
```

- **The frame meter** has a phase for each frame: free, startup, active, recovery, hitstun, blockstun, down or thrown. Frames of hitstop and of a super's flash are not counted, so a move's run is its frame data.
- **Frame advantage** is counted from the last hit or block until both sides have recovered, from the attacker's side.
- **The combo** counts its hits and damage until the defender is out of it.
- **The input history** has one line per pad, with the frames it was held.
- **`Recorder`** keeps a player's pads (up to 30 seconds) and plays them back looped in place of the player: training's dummy.
- **Frame data** (`frame_data(system, fighter, move)`) is a move list's row. The frames come from the data: startup (its first active frame), active, recovery, total, damage and chip. The advantage on hit and on block is measured by playing the move. Every number is what the game does, never typed in by hand.
  - The move is played against a copy of its fighter, standing still, then guarding (low for a low hit).
  - The guard starts before the attack, so a block is not a deflect.
  - It is played from where its first strike reaches: the attacker walks in from the round's start until it does.
  - Air moves, throws and moves without a strike are listed but not measured.
- **`demo(system, attacker, dummy, move, guard)`** is the setup those numbers come from: the match as the move begins, in reach, with the attacker's pads (from `gongfu.ai`'s `pads_for`) and the dummy's guard. A move list plays it to show the move, and the frames it shows are the frames it lists.

`tests/training_test.cpp` checks these readings against the data:
- a jab's meter shows 3 startup, 2 active and 7 recovery frames, on whiff and on hit;
- the advantage of a jab and a heavy, on hit and on block, is the stun minus what the attacker has left;
- a jab cancelled into a fireball reads 2 hits and 900 damage;
- the input history holds the pads and their frames;
- the dummy plays back exactly;
- frame data: a jab's and a heavy's frames from the data, their advantage on hit and on block as played (equal to what the data declares), a block not a deflect under the system's own rules, and a projectile measured from where it was thrown;
- a demonstration played again from its start gives the same advantage.

These tests also run on wasm.
