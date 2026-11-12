# xengine.matter, v0.1 — the reference step

What `Program::step` computes, exactly. A carrier's own path (the lattice's program, a device's kernel) gives the same bits or is wrong.

- **State** is channel-major, `[channel][instance]`, unsigned 32-bit. Channel 0 is the material's id. A value of a channel or a number column with `fraction` f is the written value × 2^f, rounded to nearest when prepared. An instance whose material id is not one of the definition's is copied and takes no part: no law acts on it, nothing flows to or from it, and it reads as 0 where a neighbour sums a property or a channel of it.
- **Input and output** are disjoint and of `instances × channels` values. A pair names two different instances that exist. A failure (`shape`, `pair`, `unstable`, `unprepared`) is found before any write.
- **Arithmetic**: `sat_add(a, b)` stops at 2^32 − 1; `sat_mul` likewise; `a − min(a, b)` stops at 0. A **term** reads a value (a whole number; a property of the instance's material; a channel of the instance; a `near.` sum), computes `sat_mul(value, multiply) >> shift`, and is added or taken. A **sum** is `plus − min(plus, minus)`, where `plus` and `minus` are the saturating sums of the terms added and of the terms taken, in the order written.
- **Units**: when a definition is prepared, the terms of a comparison are brought to the largest `fraction` any of its names has, and the terms of an amount to its target's: a whole number is × 2^f; a name of fraction g has its `multiply` × 2^(f−g), or its `shift` + (g−f). Phases compare as their index (solid 0, soft 1, granular 2, liquid 3, gas 4); `none` is 2^32 − 1.

One step, every read from the input:

1. **Degree and stability.** For each instance, `degree` = the saturating sum of the weights of its pairs. For each transfer law, with `most` its `by` column's upper bound (1 when it has none): if `most × multiply × degree > 2^shift` for any instance, the step is refused as `unstable`.
2. **Near.** For each thing some law reads as `near.x`: for each pair, each end gains `sat_mul(x of the other end, weight)`, saturating. A property is the other's material's as written (not adjusted); an end that is no material of the definition gives 0.
3. **Flows.** For each transfer law and each pair, with `a`, `b` the two ends' values of the quantity: nothing when equal; else `conductance` = the lesser of the two ends' `by` property (1 without one), `flow = sat_mul(sat_mul(sat_mul(|a − b|, conductance), weight), multiply) >> shift`; the end with more has `lost` + flow, the other `gained` + flow, both saturating.
4. **Properties.** The instance's material's row. If the definition has adjustments: a copy of it; for each adjust law in order whose condition holds — judged on the row as written — each of its amounts (also from the row as written) is added to (`raise`, saturating) or taken from (`lower`, stopping at 0) the copy. Every other law reads the copy.
5. **Reactions and conversions**, in the laws' order, each whose condition holds:
   - `add`: `plus[channel]` + amount, saturating; `take`: `minus[channel]` + amount, saturating; `set`: the first law that sets a channel gives its `base`;
   - `flag`: that channel's `base` is 1 (a flagged channel's `base` is 0 when no law that flags it holds);
   - `become`: if no earlier law of this step has, and the instance's property names a material other than its own: that material, by this law.
6. **Commit**, per channel but the material: `v` = `base` when one was set, else the input; `v = sat_add(v, plus)`; `v = v − min(v, minus)`; for each transfer of this channel: `v = v − min(v, lost)`, then `v = sat_add(v, gained)`. If the instance becomes another material and the channel `resets`: `v` = the new material's start of it (its `from` property where that is not 0, else the channel's `initial`). The material is the one it becomes, or the input's.
7. **Events**, when a vector is given, in instance order; for an instance, its channels in order then its conversion: a channel that names them went from 0 to not 0 (`rose`) or back (`fell`); the instance `became` (the law, from, to).

The result does not depend on the order of the pairs or on which end of a pair is written first (every accumulation is a saturating sum of non-negative terms into its own slot). It does not depend on the tick: a carrier that moves instances (a lattice's exchange) owns that.

Limits by default: 64 columns, 16 channels, 4096 materials, 128 laws, 16 terms a sum; a transfer's `shift` at most 31. Beyond one, preparing is refused as `matter.limit` (kind `refused`); everything malformed is `invalid-argument`.
