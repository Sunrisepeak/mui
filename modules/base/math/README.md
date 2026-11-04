# openmui.math

The repository's mathematics, one implementation each (D-87, [math-fighter design](../../../.agents/docs/2026-10-29-math-fighter-tech-design.md) §3). An independent component (D-55): the standard library only. It grows only with users: curves, vectors, fixed point and batches come when something uses them.

```cpp
import openmui.math;

const double y = omui::math::det::sin(x);            // the same bits everywhere
omui::math::SplitMix64 rng{seed};
const double u = omui::math::unit24(rng.next());      // [0, 1)
const double v = omui::math::unit53(omui::math::mix64(key));   // a number from a key, no stream
```

| Partition | |
|---|---|
| `:det` | Elementary functions with the same bits on every platform: exp, log, sin, cos, atan, atan2, cbrt, pow (fdlibm, the five correctly rounded operations only), sqrt, abs, clamp, mix, exact truncating `fmod`, and π. Moved from `hanim:math`: `hanim::dm` is this namespace. |
| `:random` | splitmix64: `golden`, its finaliser `finalize`, `mix64` (a stream's output for a state: a number from a key), the stream `SplitMix64`, and `unit24` / `unit53` ([0, 1) from a number's top bits). |
| `:color` | `color::relative_luminance(r,g,b)` for finite normalized sRGB and `color::contrast_ratio(luma_a,luma_b)`; shared [WCAG readability math](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html), using deterministic `pow`. |

Determinism is said by the namespace: `det::` is bit-exact everywhere; the random functions are integer arithmetic. The package is built without fused multiply-adds or fast-math.

`tests/math_test.cpp`: splitmix64 against Vigna's reference stream (and the first output at compile time), `mix64` as a stream's output, the ways to [0, 1); `det`'s bits pinned over a sweep.

A package does not write its own splitmix64 or fdlibm: the lint rule `math` points it here.

`det::fmod(x, y)` retains the dividend's sign and signed zero. It uses the
same exact finite remainder operation as the trigonometric reducer, avoiding
the cancellation and overflow of `x - y * floor(x/y)` at extreme ratios.
Invalid domains return a canonical quiet NaN; an infinite divisor retains a
finite dividend. The raster dash walker uses this common operation to wrap
large finite offsets. Its CTS includes exact integer witnesses at the largest
finite double and subnormal phases; no caller implements another reducer.
