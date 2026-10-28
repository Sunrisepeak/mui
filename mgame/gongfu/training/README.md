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

`tests/training_test.cpp` checks these readings against the data:
- a jab's meter shows 3 startup, 2 active and 7 recovery frames, on whiff and on hit;
- the advantage of a jab and a heavy, on hit and on block, is the stun minus what the attacker has left;
- a jab cancelled into a fireball reads 2 hits and 900 damage;
- the input history holds the pads and their frames;
- the dummy plays back exactly.

These tests also run on wasm.
