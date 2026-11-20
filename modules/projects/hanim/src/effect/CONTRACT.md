# Ordered effect plan contract v1

`EffectSpec` is preparation input, not a backend resource handle.
`PreparedEffects` owns immutable ids, parameter names and endpoints; its
metadata/output order is exactly input order. Duplicate effect ids are valid.
Parameter names are unique within one entry, not across the whole chain.

`prepare` returns `EffectError` for invalid limits, invalid keys, duplicate
names, nonfinite endpoints or capacity overflow. It never alters an existing
plan. Empty input/default construction represents a valid no-effect chain.
Preparation may allocate. Copies retain immutable shared ownership.

`sample` accepts finite progress, clamps it to [0,1], delegates interpolation
to `hanim.core` and writes caller-owned floats. Too-small output or nonfinite
progress writes nothing. Extra output is untouched. All entries use the same
progress; there are no hidden per-effect clocks, easing or elapsed-time state.
Sampling has no mutable shared scratch and may run concurrently with separate
output buffers; warm successful sampling does not allocate or format logs.
The trace implementation's initial thread-buffer attachment is outside that
warm guarantee. No numerical/random/render algorithm is reimplemented here.

Metadata spans borrow a retained plan; spans from temporary plans are rejected
at compile time. Sampler output is independent of plan storage and can be
reused after a sample; renderer binding must separately retain any needed plan.

Backend binding must explicitly validate effect existence, parameter names,
ranges for both endpoints, resource generations and actual execution support.
Representing a custom effect is not successful binding or execution. There
is no implicit unknown-effect fallback. `fx::chain(A,B)` means first A then B;
material mixing on one current geometry is a separate operation.
