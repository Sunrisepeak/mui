# hanim

A general animation engine in C++23 modules. It depends on the standard library only and knows nothing of UI, so a game, a video composition or another framework can use it as is.

**Every animation is a pure function of time.** The value at time *t* is computed from *t* and the parameters alone, so a frame can be rendered in any order, a game can rewind, and a test is reproducible bit for bit. Results are **the same bits on every platform**: the elementary functions are fdlibm transcriptions (`hanim::dm`), the package is compiled with `-ffp-contract=off`, and the tests pin hashes of sampled curves that GCC and Clang on Linux, macOS and Windows must all reproduce.

| Partition | Contents |
|---|---|
| `:math` | `dm::exp/log/sin/cos/atan/atan2/cbrt/pow` with identical bits everywhere |
| `:time` | `Clock` (interactive, virtual, render), `FrameRate` with exact frame times, motion-blur sub-frames |
| `:ease` | `Ease` as data: the standard curves, CSS `cubic-bezier` and `steps`, text form and parser |
| `:value` | `lanes<T>` / `Animatable`; `Vec2..4`, `Rgba` interpolated in premultiplied OKLab, `Transform2D` |
| `:motion` | `Tween`, `Track` (keyframes, holds, cuts), analytic `Spring` / `Oscillator`, `Decay` / `Glide`, interruptible and seekable `Motion`, `interpolate` |
| `:path` | `MotionPath`: position and heading by arc length |
| `:timeline` | `Timeline`: clips with time maps (rate, trim, repeat, alternate, reverse, fill), series, stagger, nesting, markers |
| `:set` | `Set`: the scalar channels of a running UI, evaluated in one pass without allocating |

```cpp
import hanim;
using namespace hanim;

Motion<Vec2> drawer({-320, 0});
drawer.spring_to(seconds(0), {0, 0}, springs::gentle);
drawer.spring_to(seconds(0.12), {-320, 0});   // interrupted: velocity carries over
Vec2 at = drawer.value(seconds(0.3));          // any time, any order

Timeline intro;
intro.at(seconds(0), "title.opacity", Tween<double>{.from = 0, .to = 1, .duration = seconds(0.4)})
     .then("title.y", Spring<double>{.from = 40, .to = 0, .params = springs::wobbly})
     .marker(seconds(1), "beat");
```

Tests: `mcpp test -p hanim`.
