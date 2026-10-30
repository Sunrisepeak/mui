# Subject: animation — hanim, openmui's animation engine

**What:** `modules/base/hanim` (time model, easing, springs, motion, sets, timelines, values, deterministic math via `openmui.math`) and how the framework uses it: view transitions, glyph motion, canvas camera, the page's time (seek, rewind), the promo film's timeline (`modules/media/timeline`), skeletal and presentation motion in `mgame/gongfu/present2d`. Read the READMEs, the animation sections of the spec and plan, tests (golden curves, springs).

**Top 3:** Rive (runtime, state machines, bones, blend) · GSAP (timelines, easing, sequencing, scroll) · Lottie (interchange, After Effects export). **Reference:** Spine (skeletal), Framer Motion (springs and layout animation), Core Animation.

**Dimensions:**
- `model` — animation as a pure function of time: seeking, rewind, determinism; versus each top-3's model.
- `expressiveness` — easing, springs, keyframes, paths, stagger, sequencing, state machines, blending, layout animation, text/glyph animation.
- `authoring` — how an animation is written (code, data files, tools); designer workflow vs Rive editor / AE+Lottie / GSAP code.
- `skeletal_character` — bones, meshes, deformation, IK, secondary motion (gongfu present2d, openmui.rig if present) vs Spine and Rive.
- `integration` — with UI (transitions, interruption, reduced motion), with the game, with video export.
- `performance` — cost per animated value, 1000 springs, allocation; measured.
- `determinism_tooling` — golden tests, cross-platform equality, inspection/debugging tools.

**Images:** frames of motion (fighting, promo, site glyph switch) where they exist.
