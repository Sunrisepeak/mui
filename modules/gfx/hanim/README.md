# hanim graphics-facing packages

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

This directory is currently a grouping, not the public `hanim` umbrella.
That thin compatibility umbrella still resides in
[base/hanim](../../base/hanim) and exports core only. The complete product
entry point will move here when the actual object library, effects and Engine
can be exported without creating a base-to-gfx dependency cycle.

The public player first supports strict named-part contour/transform/fill
morphs and ordered effect parameters; the optional vector adapter records the
current geometry through the existing renderer. Pixel/glyph/rig domains, full
clip composition and catalog, UI adapter and the complete product umbrella
remain implementation work.
