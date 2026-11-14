# Motion

Animation is hanim's: interface motion, animation made by code, video and game animation are one engine. Three things about it decide how it is used:

- **An animation is a function of time.** Any moment is computed directly, so it can be sought to, played backwards, and made again bit for bit.
- **It can be interrupted.** A change of mind half-way carries on from where it is and how fast it moves; nothing jumps.
- **It is data.** An animation is a document that can be checked, sampled and rendered, written by a person or an agent.

## In an interface: you write none

A component draws its state. How a button pressed, a panel appearing or a list reordered moves is a recipe of the theme and the style pack:

```ts
omui.theme("dark");                    // the default motion
omui.theme("dark", "reduced_motion");  // less of it; the user's system setting adds this layer too
```

For another feel, change the style pack or the theme's motion tokens — not the components:

```ts
motion: {
  "motion.fast": { duration: 120, easing: "cubic-bezier(0.2, 0, 0, 1)" },
  "motion.spring.snappy": { stiffness: 400, damping: 32 },
}
```

Every component then moves alike, and reduced motion and the lowering of effects on a slow machine hold with nothing more.

## An animation of its own: a document

```json
{
  "version": 1, "size": [480, 320], "background": "#111827",
  "objects": [
    { "id": "toast", "shape": { "rect": { "size": [300, 56], "radius": 14 } }, "fill": "#1f2937", "at": [240, 260] },
    { "id": "icon",  "shape": { "circle": 12 }, "fill": "#34d399", "at": [118, 260] }
  ],
  "program": { "do": "sequence", "items": [
    { "do": "parallel", "items": [
      { "do": "rise_in", "object": "toast", "distance": 40, "duration": 0.4 },
      { "do": "rise_in", "object": "icon",  "distance": 40, "duration": 0.4 } ] },
    { "do": "pulse", "object": "icon", "scale": 1.25 },
    { "do": "wait", "duration": 1.6 },
    { "do": "sink_out", "object": "toast", "distance": 20 }
  ] }
}
```

```sh
hanim check  toast.animation.omui.json                       # duration, tracks, markers — or every problem where it is
hanim sample toast.animation.omui.json --at 0.2 --at 1       # where each object is at those moments, its size and colour
hanim render toast.animation.omui.json --out toast --frames 16 --webp 30   # frames, a strip, an animated picture
```

- Steps compose: `sequence`, `parallel`, `stagger`, `loop`, `wait`.
- Forty recipes are ready: entrances (`fade_in`, `rise_in`, `pop_in` …), exits, emphasis (`pulse`, `shake`, `tada` …), idling (`bob`, `breathe` …), travel (`arc_to`, `swap`, `reorder`).
- [Twenty documents to take away](../../../modules/projects/hanim/studio/examples/README.md): a loader, a toast, a success check, a like, a card shuffle, a page transition, confetti …

## In C++

```cpp
using namespace std::chrono_literals;
auto trip = hanim::sequence(
    hanim::anim::move_to(card, {120, 40}).duration(300ms),
    hanim::anim::rotate_by(card, .12));
auto playback = engine.play(trip);      // then paused, sought, given another target
```

More: [hanim's README](../../../modules/projects/hanim/README.md). A whole film made this way: the [promo video](../../../showcase/promo-video).
