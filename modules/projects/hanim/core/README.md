# hanim.core

The renderer-independent deterministic core of the hanim animation product, in C++23 modules. It depends on the standard library and `mlibs.math` only (its deterministic functions, D-87) and knows nothing of UI, so a game, a video composition or another framework can consume it directly. The public [hanim umbrella](..) re-exports these same entities and adds optional object/player modules; pixel-domain animation remains separate work.

**Every animation is a pure function of time.** The value at time *t* is computed from *t* and the parameters alone, so a frame can be rendered in any order, a game can rewind, and a test is reproducible bit for bit. Results are **the same bits on every platform**: the elementary functions are fdlibm transcriptions (`hanim::dm`), the package is compiled with `-ffp-contract=off`, and the tests pin hashes of sampled curves that GCC and Clang on Linux, macOS and Windows must all reproduce.

| Partition | Contents |
|---|---|
| `:math` | `dm::exp/log/sin/cos/atan/atan2/cbrt/pow` with identical bits everywhere |
| `:time` | `Clock` (interactive, virtual, render), `FrameRate` with exact frame times, motion-blur sub-frames |
| `:ease` | `Ease` as data: the standard curves, CSS `cubic-bezier` and `steps`, text form and parser |
| `:value` | `lanes<T>` / `Animatable`; `Vec2..4`, `Rgba` interpolated in premultiplied OKLab, `Transform2D` |
| `:motion` | `Tween`, `Track` (keyframes, holds, cuts), analytic `Spring` / `Oscillator`, `Decay` / `Glide`, interruptible and seekable `Motion`, `interpolate` |
| `:path` | `MotionPath`: position and heading by arc length; `PointPath`: reusable chord-relative point interpolation |
| `:shape` | `PreparedShapeMorph`: renderer-independent, prepared single-contour Bezier correspondence; allocation-free sampling |
| `:timeline` | `Timeline`: clips with time maps (rate, trim, repeat, alternate, reverse, fill), series, stagger, nesting, markers |
| `:set` | `Set`: the scalar channels of a running UI, evaluated in one pass without allocating |

```cpp
import hanim.core;
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

Tests: `mcpp test -p hanim.core`.

`Ease::valid()` validates data supplied by another subsystem before it publishes
a prepared curve: finite parameters, legal Bezier x controls and integer step
counts/positions. Overshoot remains legal. Program preparation and the optional
timeline/audio adapter share this validation; it does not alter curve sampling.

`ShapeMorphOptions::point_path` optionally changes matched vertex/control-point
trajectories. `PointPath::bend(0.25)` moves a midpoint by one quarter of its
endpoint chord length along `(-dy,dx)`. Two custom control offsets can describe
an asymmetric cubic trajectory. The operator also works directly on any Vec2
pair; it is neither tied to contours nor to UI. Finite bounded controls are
validated during preparation. Zero controls keep the original direct sampling
loop. Endpoints, correspondence and exact contour connectivity are preserved;
seek order never changes results. It does not guarantee simple topology or
constant-speed travel. See [typed effect semantics](../src/effect/README.md).

The graphics consumer in [render/vector's CTS](../../xrender/vector/tests/shape_morph_test.cpp)
uses this public plan without a UI Runtime or site dependency. It renders a
single changing contour, varies a real material's lighting parameters, and
composes the existing tone and blur effects. The original and subdivided
endpoint paths are compared at 1× and 2×; independently flattening the same
curve can change coverage at the boundary, so that test declares its bounded
edge tolerance and requires exact interior/exterior pixels.

![A single triangle contour becoming a circle with a changing material](../media/triangle-circle-material.webp)

This is a first geometry checkpoint. It does not yet provide the proposed
pixel correspondence, arbitrary topology changes,
simulation, or a complete reusable animation catalog. Only the prepared
plan's sampling is allocation-free; the graphics test also builds display
lists and encodes review artifacts.


`PreparedShapeMorph` prepares a real geometry transition independently of UI,
DisplayList, shaders or the website. For example, this triangle can become a
four-cubic circle; the consumer receives one current closed contour, with no
crossfade fallback:

```cpp
import std;
import hanim.core;
using namespace hanim;

const std::array triangle{ShapeSegment::line({-1, 1}),
                          ShapeSegment::line({1, 1})};
constexpr double k = 0.5522847498307936;
const std::array circle{
    ShapeSegment::cubic({k, -1}, {1, -k}, {1, 0}),
    ShapeSegment::cubic({1, k}, {k, 1}, {0, 1}),
    ShapeSegment::cubic({-k, 1}, {-1, k}, {-1, 0}),
    ShapeSegment::cubic({-1, -k}, {-k, -1}, {0, -1})};
auto plan = PreparedShapeMorph::prepare({{0, -1}, triangle}, {{0, -1}, circle});
if (plan) {
    std::vector<ShapeCubic> current(plan->segment_count()); // allocate once
    auto written = plan->sample(0.5, current);             // no allocation
    // Each cubic has start, control1, control2, end. Feed this span to your
    // renderer or geometry consumer; sample other times into the same buffer.
}
```

Preparation owns the result, so input spans need not outlive it. Lines and
quadratics are exactly elevated to cubics; both original sets of curve
boundaries are retained, and de Casteljau subdivision preserves the original
Bezier endpoint geometry and sharp corners, up to floating-point roundoff.
The example circle is itself the familiar four-cubic approximation of a
mathematical circle; preparation introduces no polygon approximation.
A fixed chord table estimates arc length for correspondence. Target winding
and cyclic start can be aligned; `target_reversed()` and
`target_start_fraction()` report those choices. Alignment changes traversal,
not the shape's coordinates. Disable either alignment when traversal is an
explicit artistic input.

The first checkpoint accepts **one simple, closed, nonzero-area 2D contour per
endpoint**. The caller is responsible for simplicity: this API does not certify
curve self-intersections. Holes, multiple contours, topology splitting/merging,
optimal transport and guarantees of simple intermediate shapes remain outside
this checkpoint. A shape can be concave. Nonzero-area closed cubics are allowed
even when their start and end are equal. Individual constant segments are
removed; a wholly constant or numerically near-zero-area contour is rejected.
Area magnitudes at or below `1e-12 * control_bounds_extent²` are treated as
numerically degenerate. Coordinates must be finite and within `[-1e100,1e100]`;
this conservative numeric domain bounds preparation arithmetic. Lengths use
scaled norms and signed area uses normalized coordinates, avoiding squared
length / area underflow for uniformly tiny inputs. A positive-length edge
whose normalized arc interval is unrepresentable returns `numerical_range`
instead of silently disappearing. It is not a screen-coordinate restriction.

`prepare` returns `std::expected` errors for open/empty/degenerate inputs,
invalid verbs, nonfinite/out-of-range coordinates, invalid options and segment
budgets. `max_input_segments` includes implicit closure; `max_output_segments`
includes all corresponding subdivisions. Preparation uses bounded heap scratch
and may fail the budget; it never silently changes a requested morph to a fade.
It does not turn allocation failure into a recoverable geometry error.

`sample` is a const pure function of progress and the prepared plan. It writes
only `segment_count()` cubics into caller storage. Finite progress clamps to
`[0,1]`; NaN/infinity, an undersized buffer or a default-constructed unprepared
plan return an error before touching output. Sampling uses no heap allocation
or mutable caches, supports arbitrary seek/reverse order and concurrent reads
with separate output buffers, and has O(prepared segment count) work. A caller
can prepare a new plan from sampled cubics for positional continuity;
velocity-preserving retargeting belongs to the future object/player layer.
`sample_segment(progress, index)` returns one cubic by value with the same
clamping/error policy and no allocation. It supports direct streaming into a
consumer-owned arena; `invalid_segment` reports an out-of-range index.
This is the reusable contour preparation primitive, not the complete HObject,
Engine, material transition, pixel correspondence or Manim feature set.

The independent `shape_test` consumer checks different curve types,
triangle→circle and concave-star→circle endpoints/intermediates, winding/phase,
seek and re-preparation, invalid inputs/budgets, atomic output failures, and
10,000 samples with scalar/array allocation instrumentation. These are geometry
CTS checks; renderer pixel comparisons and UI adapter integration are separate
acceptance work.

The separate manifest is intentional: UI motion, the rasterizer and the media
timeline all directly share this std-plus-math library. Keeping it below the
product excludes object registries, player, renderer and observability
dependencies from those consumers; the product re-exports these same entities.


`MotionPath` exposes `segment_count()`, `valid()` (finite coordinates and monotone
finite distance tables) and `continuous()` (no inter-segment position jumps).
These inspections are cold preparation tools; they do not make the mutable
builder thread-safe. `point(u)` and `at(u)` share the same distance lookup, but
position-only consumers do not calculate a tangent heading. The estimate still
uses 32 chord samples per cubic; it is not an adaptive/exact arc-length solver.
For Node/Wasm tests that read reference files, select the workspace's existing
`wasm-test` profile; ordinary browser builds deliberately do not expose host files.
