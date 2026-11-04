# hanim foundations

| Package | Responsibility |
|---|---|
| [hanim.core](core) | Deterministic time, values, curves, springs, paths, prepared closed-contour morphs, timelines and channel batches; standard library and openmui.math only |

This is a grouping directory, not a second `hanim` package. The product
umbrella is [modules/gfx/hanim](../../gfx/hanim): `import hanim;` retains the
same core symbols and also exports objects, recipes, effects, programs and the
explicit-clock Engine. Rendering remains opt-in.

Existing `hanim = { path = ".../modules/base/hanim" }` dependencies must change
their path to `modules/gfx/hanim`; the package name and C++ import do not change.
Core-only users keep `hanim.core` at `modules/base/hanim/core` unchanged.
