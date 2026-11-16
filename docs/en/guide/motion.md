# Motion

Animation is hanim's: interface motion, animation made by code, video and game animation are one engine. Three things about it decide how it is used:

- **An animation is a function of time.** Any moment is computed directly, so it can be sought to, played backwards, and made again bit for bit.
- **It can be interrupted.** A change of mind half-way carries on from where it is and how fast it moves; nothing jumps.
- **It is data.** An animation is a document that can be checked, sampled and rendered, written by a person or an agent.

## In an interface: you write none

A component draws its state. How a button pressed, a panel appearing or a list reordered moves is a recipe of the theme and the style pack. An application only chooses them; [example 06](../../../examples/06-theming) stacks a theme and its overlays, reduced motion among them (the user's system setting adds that layer too):

<!-- code: examples/06-theming/logic/main.ts#compose -->
```ts
// The composition the controls say: a theme, then overlays.
function compose(): void {
  const layers: string[] = [scheme.get()];
  if (compact.get()) layers.push("compact");
  if (calm.get()) layers.push("reduced_motion");
  try {
    theme(layers);
    problem.set("");
  } catch (error) {
    problem.set(String((error as Error).message));
  }
  current.set(themes().current);
}
```

For another feel, change the style pack or the theme's motion tokens — not the components. The reduced-motion overlay of omui's default themes is such a change, each motion token made the instant one:

<!-- code: modules/projects/omui/theme/packs/default/default.theme.xdef.ts#reduced-motion -->
```ts
// The person asked for less motion (spec/registry/motion.json, reduced_motion):
// every transition shows its target at once, springs settle at once.
reduced_motion: {
  doc: "Reduced motion: transitions show their targets at once.",
  overlay: true,

  motion: {
    "motion.fast": "motion.instant",
    "motion.normal": "motion.instant",
    "motion.slow": "motion.instant",
    "motion.enter": "motion.instant",
    "motion.exit": "motion.instant",
    "motion.spring.snappy": "motion.instant",
    "motion.spring.gentle": "motion.instant",
    "motion.spring.bouncy": "motion.instant",
  },
},
```

Every component then moves alike, and reduced motion and the lowering of effects on a slow machine hold with nothing more.

## An animation of its own: a document

The toast, one of hanim studio's documents:

<!-- code: modules/projects/hanim/studio/examples/toast.animation.xdef.json -->
```json
{
  "version": 1,
  "size": [480, 320],
  "background": "#111827",
  "objects": [
    { "id": "toast", "shape": { "rect": { "size": [300, 56], "radius": 14 } }, "fill": "#1f2937", "stroke": { "color": "#374151", "width": 1 }, "at": [240, 260] },
    { "id": "icon", "shape": { "circle": 12 }, "fill": "#34d399", "at": [118, 260] },
    { "id": "line", "shape": { "rect": { "size": [150, 10], "radius": 5 } }, "fill": "#e5e7eb", "at": [225, 252] },
    { "id": "subline", "shape": { "rect": { "size": [110, 8], "radius": 4 } }, "fill": "#6b7280", "at": [205, 270] }
  ],
  "program": {
    "do": "sequence",
    "items": [
      { "do": "parallel", "items": [
        { "do": "rise_in", "object": "toast", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "line", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "subline", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "icon", "distance": 40, "duration": 0.4 }
      ] },
      { "do": "pulse", "object": "icon", "scale": 1.25 },
      { "do": "marker", "name": "shown" },
      { "do": "wait", "duration": 1.6 },
      { "do": "parallel", "items": [
        { "do": "sink_out", "object": "toast", "distance": 20 },
        { "do": "sink_out", "object": "line", "distance": 20 },
        { "do": "sink_out", "object": "subline", "distance": 20 },
        { "do": "sink_out", "object": "icon", "distance": 20 }
      ] }
    ]
  }
}
```

```sh
hanim document.check  --file toast.animation.xdef.json                       # duration, tracks, markers — or every problem where it is
hanim document.sample --file toast.animation.xdef.json --at 0.2 --at 1       # where each object is at those moments, its size and colour
hanim document.render --file toast.animation.xdef.json --out toast --frames 16 --webp 30   # frames, a strip, an animated picture
```

- Steps compose: `sequence`, `parallel`, `stagger`, `loop`, `wait`.
- Forty recipes are ready: entrances (`fade_in`, `rise_in`, `pop_in` …), exits, emphasis (`pulse`, `shake`, `tada` …), idling (`bob`, `breathe` …), travel (`arc_to`, `swap`, `reorder`); [each recipe through its course](../../../modules/projects/hanim/RECIPES.md).
- [Twenty documents to take away](../../../modules/projects/hanim/studio/examples/README.md): a loader, a toast, a success check, a like, a card shuffle, a page transition, confetti …

## In C++

From hanim's recipe tests: two objects exchange places — played, paused, sought, and each moment read (`take` unwraps a result there):

<!-- code: modules/projects/hanim/tests/program/recipes_test.cpp#swap -->
```cpp
// Two objects.
Engine engine = take(Engine::make());
const auto a = take(engine.create_object(square({100, 100}))), b = take(engine.create_object(square({300, 100})));
const auto played = take(engine.play(anim::swap(a, b, {200, 0})));
take(engine.pause(played));
const double length = take(engine.inspect(played)).duration.count();
take(engine.seek(played, seconds(length / 2)));
const double ya = take(engine.frame(played, 0)).geometry.parts[0].local.ty, yb = take(engine.frame(played, 1)).geometry.parts[0].local.ty;
expect((ya - 100) * (yb - 100) < 0) << std::format("they pass on either side: {} and {}", ya, yb);
take(engine.seek(played, seconds(length)));
expect(near(take(engine.frame(played, 0)).geometry.parts[0].local.tx, 300) && near(take(engine.frame(played, 1)).geometry.parts[0].local.tx, 100)) << "and exchange places";
```

More: [hanim's README](../../../modules/projects/hanim/README.md). A whole film made this way: the [promo video](../../../showcase/promo-video).
