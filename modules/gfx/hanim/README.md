# hanim graphics-facing packages

The object and playback layers of the independent hanim product. These
packages do not depend on the UI framework, its specification or games.
The deterministic [hanim.core](../../base/hanim/core) remains below this
layer, using only the standard library and `openmui.math`.

| Package | Responsibility |
|---|---|
| [hanim.object](object) | Generational object/part identities and owned immutable captures of hierarchical 2D parts, actual contours and solid fills |

This directory is currently a grouping, not the public `hanim` umbrella.
That thin compatibility umbrella still resides in
[base/hanim](../../base/hanim) and exports core only. The complete product
entry point will move here when the actual object library, effects and Engine
can be exported without creating a base-to-gfx dependency cycle.

Object captures are a first supported representation, not pixel transport,
an animation library or `Engine::play`. Renderer/UI adapters, richer resource
domains and the unified player remain separate implementation work.
