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
- pins each spark style and the negative flash.

An overlay drawn over the frame (training's boxes) lines up with it through `view_camera` (the camera the frame was drawn with, shake included) and `on_picture` (a point of the stage on the picture).
