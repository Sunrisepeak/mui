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
- **The camera** keeps both fighters in view, zooming out as they part and following a jump. **Effects** are drawn from the events kept by frame: rays where a hit lands (red for a counter), a ring for a block or a throw, a flash for a KO, and the shake of a heavy hit.

Every picture is a pure function of the match's state and the kept events, its trigonometry hanim's deterministic functions: `tests/present2d_test.cpp` pins the display lists and the CPU pixels of three frames (the same on GCC and LLVM), checks the jab's reach on its active frames and the mirrored pose, and draws a frame after a rollback the same bytes as before it.

An overlay drawn over the frame (training's boxes) lines up with it through `view_camera` (the camera the frame was drawn with, shake included) and `on_picture` (a point of the stage on the picture).
