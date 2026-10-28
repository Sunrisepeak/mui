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
- **Input** (`:input`): pads with four directions and six buttons.
  - The log keeps 64 frames of directions relative to the fighter's facing, with the buttons held and pressed. It lives in the state, so a rollback restores what the inputs were.
  - SOCD is resolved as neutral, last wins or up wins.
  - Commands:
    - motions in a window (236, 214, 623, 63214);
    - dashes, a direction tapped twice with no button (66, 44);
    - charges (back or down held, then the other way);
    - a direction held at the press;
    - buttons, any one or all together;
    - a buffer.

  When moves of the same priority match, the more particular command wins, so 2HP is not read as 5HP.
- **Data** (`:data`): a fighter's body and moves, and the system's rules. `compile` checks a fighter and resolves its cancels.
  - A move has:
    - its frames;
    - hits: boxes, damage, chip, hitstun, blockstun, hitstop for each side, pushback, level, knockdown with its own frames down, launch with juggle points, juggle cost, and the impact profile that drawing reads;
    - cancels on contact;
    - invulnerability by kind (strikes, air attacks, projectiles, throws);
    - its own hurtboxes on some frames;
    - movement steps;
    - throws, forward or back (the two change places);
    - projectiles: a velocity, so one can fall; several hits with an interval; strength in a clash; a limit on screen;
    - meter cost and gain, and a super's freeze.
  - The system's rules: combo scaling, counter hits, the round, the stage, proximity guard and SOCD.
- **The match** (`:match`):
  - **Each frame:** both fighters decide from the frame's start, move, are pushed apart and kept on the stage. Strikes, throws and projectiles are all found before any is applied, so two on one frame trade.
  - **Blocking:** high blocks either way, low only crouching, overhead only standing. Holding back near a move guards in place.
  - **Throws:**
    - A throw breaks when both throw, or when the defender presses LP+LK within its window from the frame it takes hold.
    - No one is thrown in the frames just after waking up, hitstun or blockstun.
    - A fighter about to jump can be thrown.
  - **Hitstop and flashes:**
    - Each fighter stands still for its own hitstop. A projectile's owner keeps moving.
    - In a super's flash everything stands still, including the timer and invulnerability.
    - A move asked for while standing still starts on the first frame after.
  - **Juggles:**
    - A launch gives juggle points, and each hit in the air spends some.
    - With too few left, the defender air-resets: it falls untouchable and lands free.
    - A defender can be launched again only once in a juggle.
  - **Meter:** a defender gains a tenth of the damage it takes.
- **The state** (`:state`): one block of plain data without padding (checked at compile time); `checksum` is the hash of its bytes.

`tests/rules_test.cpp` checks system 0.2's rules one by one ([characters design](../../.agents/docs/2026-10-28-gongfu-characters-design.md) §2, G-1..G-10):
- dashes, and a back dash that strikes pass through but throws do not;
- projectiles that fall, hit three times, outweigh another and are limited on screen;
- back throws and the frames no one can be thrown;
- an anti-air invulnerable only to the air;
- a stretched arm that can be hit;
- a target combo;
- juggle points ending in an air reset;
- hitstop by side, and input kept through a flash;
- meter for the defender, and a knockdown's own length.

It also plays 5,000 frames of all of these, rolled back every frame, against a straight run. The hash is pinned and the same on GCC, LLVM and wasm.

`tests/gongfu_test.cpp` holds the numbers, the input reading, the data's problems, movement, frame data measured as declared (startup, active, recovery on whiff; advantage on hit and block = stun − what the attacker has left), blocking, cancels and combo scaling, counter hits, throws and breaks, projectiles and clashes, invulnerability — and 10,000 rollbacks of random depth over 10,000 random frames leaving every frame's state the bytes of a run without them. The run's hash is pinned: GCC, LLVM and wasm32-emscripten play those inputs into the same bytes.

## Inside

- [present2d](present2d) (`gongfu.present2d`): a match drawn in 2D: skeletons with vector parts posed by clips, the camera, and impact profiles (shake, punch in, flash, sparks). Looks are files (`*.look.gongfu.toml`). Every picture is a pure function of the state and its events.
- [training](training) (`gongfu.training`): what training mode shows, measured from the match after each frame:
  - each player's input history;
  - the frame meter: startup, active, recovery and stun, with hitstop not counted;
  - the frame advantage once both sides recover;
  - the combo's hits and damage;
  - a recorder that plays a player's pads back, looped (the dummy).

  It uses only the standard library and allocates nothing per frame.
- [studio](studio) (`gongfu.studio`): making fighters. The library and the command line (`gongfu-studio`, JSON output and exit codes) for people, agents and CI:
  - `bake` writes each fighter's hurtboxes from its look's animation (`<name>.boxes.gongfu.toml`): where a limb reaches out of the stance's box, frame by frame (G-5);
  - `check` says whether those files are current;
  - `embed` gives a program its fighters.

  The graphical studio comes later, on the same library.
- [rules](rules) (`gongfu.rules`): the build rule. A program's fighters (with their looks and baked hurtboxes) become a module partition it carries, and a stale bake fails the build, naming the command to run. The simulation never samples an animation.
- [data](data) (`gongfu.data`): characters and rules as files — `*.fighter.gongfu.toml`, `*.system.gongfu.toml` — read with every problem at its line and column, moves in numpad notation (`236P`, `[4]6P`, `2LK`, `LP+LK`).
