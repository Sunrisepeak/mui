# gongfu.present2d

What a gongfu match looks like in 2D (D-85 §8.4), drawn into openmui's display list — by its CPU reference renderer or its GPU renderer.

```cpp
gongfu::present2d::Effects effects;
match.tick(p1, p2);
effects.record(match);                       // the frame's hits, blocks, throws, KOs
omui::render::DisplayList frame;
gongfu::present2d::draw(match, left_look, right_look, effects, scene, frame);
// after a rollback to `f`: match.restore(saved); effects.rewind(f + 1); and play forward again
```

- **A look** is a skeleton (bones with a joint on their parent, a length and a rest angle) with vector parts (capsules, ellipses, polygons; an outline drawn behind them) and clips — each bone's angle and the body's rise, keyed by frame with hanim's easing.
- **The clip** is what the fighter does: its move by name, else its mode (`idle`, `walk_forward`, `crouch`, `hitstun`, `knockdown`, …). A move's clip is read at the move's frame, a stun's at the frames it has taken, the rest at the match's frame, looping; the fighter facing left is the mirror of the one facing right.
- **The camera** keeps both fighters in view, zooming out as they part and following a jump.
- **Impacts** say how a contact feels, drawn (D-85 §8.4).
  - A fighter file's hits name a profile (`impact = "heavy"`), and the look defines it:
    - the shake (pixels, frames);
    - the camera's punch in;
    - a flash (white, or the negative);
    - the spark: `rays`, `ring`, `splash` (ink), `shards` (glass) or `bolt`, with its size, count, life, colour and counter colour;
    - the sounds and the rumble.
  - Blocks, throws and KOs use the look's `block`, `throw` and `ko` profiles. Anything without a profile gets a default.
  - Effects are drawn from the events kept by frame. Their scatter is a hash of the event, so a spark is the same after a rollback.
- **A level 3's cinematic** (`[cinematic.<move>]`) is a timeline keyed by the lock's frame: what each fighter plays; the camera's keys; a backdrop, flashes, quakes and rings; cut-in panels; effects at places, over the fighters or `behind` them (a dial standing behind the bodies). The bodies really meet:
  - paths place each fighter, a key's ease the way to it (`out_quad` up, `in_quad` down: a body flung and falling);
  - turns draw a fighter facing the other way (a shoulder driven in backwards, a catch behind the back);
  - holds keep the defender at a bone of the attacker, playing each hold's clip from its first frame, so holds in a row show a held body reacting to each blow;
  - each hit asks the defender for a reaction (`hitstun.head` … or `held`, `launched`, `slammed`; a look without `launched` or `slammed` gives its heavy hit's), and in a hitstop the struck shakes and its reaction waits;
  - afterimages trail the attacker over the frames they say, and `in_front` draws the attacker over the defender (a knee driven into a body).
  - a stop stands time still: the picture grey toward its tint (shapes and drawn art alike), the struck as it was, the sparks and effects of blows landing in it hanging in the air; the blows take effect when time runs again.
- **Set pieces** (G-15, panes) are drawn by their owner's look: glass, an edge and a shine, cracks as they wear, or drawn art (`pane = { …, picture = "fx", whole = [x, y, w, h], cracked = [x, y, w, h] }`: the region standing where the pane stands, the cracked one once it has lost durability), with a gold edge on a pane that turns projectiles back.
- **Time** (刻, G-20, G-21): a clone is drawn in its look's tint, a rewind leaves ghosts along the way back, and where a mark stands its look's `mark` effect plays on the floor (`rewind = { …, mark = "…" }`); a tinted body (a clone, a ghost, an afterimage) is one silhouette at its tint's opacity.
- **Looks are files**, `*.look.gongfu.toml` (`read_look` / `write_look`):
  - palettes of named slots (the left side's and the right's);
  - bones, with angles in degrees;
  - parts;
  - clips, keyed by frame, with an ease for each clip and for each key where it differs;
  - impacts.

  Every problem is reported at once with its line and column, and a misspelt word gets the nearest right one. What `write_look` writes reads back as itself.

Every picture is a pure function of the match's state and the kept events, and its trigonometry is hanim's deterministic functions. A `Painter` reuses its poses and paths, so after the first frame, drawing one allocates nothing.

`tests/present2d_test.cpp`:
- pins the display lists and the CPU pixels of three frames, the same on GCC and LLVM;
- checks the jab's reach on its active frames and the mirrored pose;
- draws a frame after a rollback, the same bytes as before it;
- reads the training fighter's look file, writes it back and reads it again (the same picture);
- reports a broken look's problems at their places;
- resolves impacts by name and checks the camera's shake and punch in;
- pins each spark style and the negative flash;
- draws panes as glass and from a picture (whole, then cracked);
- draws a cinematic's bodies turned, eased along a path, held, in a hitstop, launched and with afterimages, each against the picture it should equal;
- stands time still: drawn art grey in a stop, the struck standing and a blow's effect hanging until the bell;
- draws a cinematic's effects behind the fighters, and over them;
- draws an afterimage as one silhouette, and a mark's effect where it stands.

An overlay drawn over the frame (training's boxes) lines up with it through `view_camera` (the camera the frame was drawn with, shake included) and `on_picture` (a point of the stage on the picture).
