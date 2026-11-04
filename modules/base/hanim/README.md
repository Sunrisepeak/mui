# hanim

The public hanim product entry point. This package currently re-exports
[hanim.core](core), which owns all existing deterministic time/value animation
and renderer-independent prepared shape algorithms. It introduces no renderer
or UI dependency.

| Package | Responsibility |
|---|---|
| [hanim.core](core) | Deterministic clocks, easing, values, tweens, springs, paths, prepared closed-contour morphs, timelines and channel batches |

Existing external consumers may keep `import hanim;`. The namespace remains
`hanim::`, and the existing core types and version constants are the same
entities through either module. Repository internals and lower-level consumers
import `hanim.core` and depend on that package directly, leaving this product
umbrella free to grow without a dependency cycle.

```cpp
import hanim;      // compatibility/product entry point
// import hanim.core;  // independent core consumer instead

hanim::Tween<double> opacity{.from = 0, .to = 1,
                              .duration = hanim::seconds(0.4)};
```

Core API, numeric boundaries, review media and conformance tests are documented
in [core/README.md](core/README.md). Run `mcpp test -p hanim.core` for those
existing algorithm and determinism checks, and `mcpp test -p hanim` for the
thin umbrella compatibility checks. This split itself adds no HObject
player, renderer, material interpolation or reusable animation catalog.
