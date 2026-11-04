# hanim — the independent animation product

The object and playback layers of the independent hanim product. These
packages do not depend on the UI framework, its specification or games.
The deterministic [hanim.core](../../base/hanim/core) remains below this
layer, using only the standard library and `openmui.math`.

| Package | Responsibility |
|---|---|
| [hanim.effect](effect) | Owned ordered parameter plans and reusable effect recipes; backend binding stays optional |
| [hanim.engine](engine) | Explicit-clock playback and batch sampling, controls, current-pose retarget and typed queries |
| [hanim.library](library) | Strict public named-part contour morph plans, caller-owned frames and deep C0 retarget preparation |
| [hanim.object](object) | Generational object/part identities and owned immutable captures of hierarchical 2D parts, actual contours and solid fills |
| [hanim.program](program) | Immutable nested bindings compiled into owned object schedules; bounded arbitrary-time sampling without a player |
| [render](render) | Optional renderer consumers: current vector frames, shared native material mixing and effects |

This is the only `hanim` package, exporting core, object, library, effect,
program and engine. `import hanim;` preserves the existing core vocabulary;
`hanim.core` stays at `modules/base/hanim/core` for minimal consumers. Local
path dependencies formerly pointing to `modules/base/hanim` must point here.
There is no UI, specification, renderer, RHI or window dependency in the product
closure. Rendering is a separate, explicitly selected adapter.

mcpp currently normalizes this existing unnamespaced package to
`mcpplibs.hanim` in resolution metadata; the dependency key and module are
`hanim`. When selecting both the umbrella and an adapter in TOML, quote both
keys (`"hanim"`, `"hanim.render.vector"`) so the dotted key is not parsed as
a nested field of the umbrella dependency.

```cpp
import std;
import hanim;
using namespace std::chrono_literals;

// source and target are ObjectRefs owned by hengine; snapshots also work.
auto animation = hanim::anim::morph(source, target)
    .duration(600ms)
    .easing(hanim::ease::in_out_cubic)
    .with_effect(hanim::fx::blur(0, 2));
auto played = hengine.play(animation);
if (!played) { report(played.error()); return; }
// ... sample/control, then release the instance before replaying its object.
if (auto released = hengine.release(*played); !released) { report(released.error()); return; }

auto prepared = hengine.prepare(animation);
if (!prepared) { report(prepared.error()); return; }
auto replay = hengine.play(*prepared);
if (!replay) { report(replay.error()); return; }
```

The leaf builder produces the existing immutable `Program`, not a second
authoring tree/compiler/player. `sequence(a,b)`, `parallel(a,b)` and
`stagger(100ms,a,b)` compose morph leaves and advanced Programs. Setters return
new leaf values; they do not capture or mutate live objects. `ObjectRef` inputs
are captured at prepare, while explicit `Snapshot` inputs stay frozen. The
owning thread must not concurrently mutate the registry; Engine preparation
rejects reentrant writes. Prepared geometry remains owned after source removal.

`play(animation, ProgramLimits)` is a cold-path convenience. For a frame-budgeted
caller, prepare explicitly and reuse `PreparedProgram`; `play(prepared)` still
reserves playback buffers. Advanced bindings and the prior
`play(object, anim::morph_to(target), effects, options)` remain available.
Effects here are ordered leaf parameters, not group/image scopes. Unsupported
topologies, invalid inputs, limits and conflicting writes return typed failures;
no fallback silently discards an effect. Outputs never write business objects.

[The external consumer](../../../tools/ci/hanim-api-probe/README.md) builds and
runs dev/release from only this package, auditing the actual dependency graph.

The public player first supports strict named-part contour/transform/fill
morphs and ordered effect parameters; the optional vector adapter records the
current geometry through the existing renderer. Pixel/glyph/rig domains, full
channel mixing and catalog, group effect scopes and the UI adapter remain
implementation work. CPU/GPU renderer support is evaluated by its own adapter
tests, not inferred from effect-parameter interpolation.
