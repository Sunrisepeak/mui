# hanim.render.vector

An optional ordinary consumer of `hanim.library` and `hanim.effect`. It records
the current contour frame into the existing `openmui.render.vector.DisplayList`;
the existing CPU renderer/rasterizer produces pixels. No UI, Engine, host,
second geometry interpolator, material mixer or effect algorithm is introduced.

`PreparedVector::bind(geometry, effects, materials)` retains immutable geometry
and effect plans. Explicit material bindings identify a named part and two
borrowed preparation paints. The existing `PreparedPaintMix` deep-owns their
solid/gradient payloads. The first adapter rejects image, image-mask, lit
material and arbitrary mixed input resources instead of silently losing them.
Gradient stops are finite, sorted in [0,1], with one to 4,096 stops. Current
solid fills and all colors are finite straight sRGB RGBA in [0,1]. A material
override replaces that part's fill channel; it uses the same current geometry
and frame progress, not two endpoint masks.

Binding checks actual registered CPU effects, names, defaults, endpoint ranges
and parameter schemas. Unknown effects/parameters, missing CPU implementations
and backdrop effects fail explicitly. Missing named controls use validated
backend defaults. Parameter inputs are mapped into the renderer's schema order.
There are at most 65,536 backend parameter values. Each record checks effect
registry generations and validates sampled values again. Effect replacement
must happen at a quiescent boundary, not concurrently with bind/record/render;
the existing registry does not provide immutable execution leases.

Allocate caller scratch from `requirements()`: world transforms, backend
parameter values, stage bounds, plus one reusable native DisplayList recording
arena. On every `record(frame, sampled_effect_values, destination, scratch)`,
the adapter verifies frame provenance/metadata, buffer sizes, finite float
geometry/transforms, connected contours and current parameter ranges before
recording. It records to scratch and appends only on success, so every returned
failure leaves the destination unchanged. Scratch may change on failure and
must be separate from destination. Both lists own their variable payloads.

This staging approach copies native command/path/material payloads once more
than a direct writer. It preserves failure atomicity using the existing arena;
it is not free. Warm recording can reuse capacity, but no whole-frame, GPU,
effect-execution allocation or performance result is implied. CPU effect
execution itself uses the existing renderer's allocation/layer behavior.

Transforms use core `Transform2D::matrix()` and existing `geom::Affine`
composition in the prepared hierarchy order. Sibling drawing order stays the
library's source order. Multiple contours of one part share one recorded path,
so the material sees one current geometry mask. Ordered chains wrap in reverse
push order and execute first-to-last on pop. An outer stage's bounds include
the previous stage's declared output reach, retaining blur halos.

Rebind after play/retarget changes the prepared geometry identity. The adapter
does not automatically transport a material binding across Engine retargets;
consumers that need material C0 must explicitly prepare their next
`PreparedPaintMix` from the sampled current material. Engine-independent
recording and actual CPU pixels are this adapter's scope; GPU/device capability
and pixel-domain object transport remain separate work.
