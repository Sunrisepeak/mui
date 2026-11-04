# openmui.memory

The frame arena (zero heap allocation in steady state), generational handles and slot maps (stale handles are detected, not dereferenced), and `FunctionRef`. Standard library only (module `openmui.memory`).


`SlotStorage<T, H, InitialGeneration>` is the single slot/free-list owner.
`SlotKey<Tag>` uses a 32-bit index and a 64-bit generation; the default initial
generation is 1. `SlotMap<T, Tag>` is the compatibility adapter with the existing
`Handle<Tag>` (32-bit index + 32-bit generation, unchanged `bits/from_bits`)
and generation-zero initial insertion. At generation exhaustion both now
retire the slot permanently instead of wrapping; this is a safety correction,
not compatibility with the former exhaustion-time wraparound.

A `SlotStorage` limit bounds materialized slots, including reusable vacancies
and retired slots. It does not preallocate the maximum and is not a byte budget
for allocator/vector spare capacity. Queries and traversal add no storage allocation; caller callbacks may allocate.
`clear()` invalidates live keys while retaining generation/retirement history.
Values/references may move when storage grows, while handles remain stable.

`try_insert(factory, capacity_error)` first previews the candidate handle, then
calls `factory(handle) -> std::expected<T,E>`. Full storage returns the supplied
capacity error without invoking the factory. Factory failure passes its error
through and leaves slot count, live count, generation, free-list and allocation
capacity unchanged. **The factory may read this storage, but must not mutate,
reenter mutation, swap, move or destroy it.** External factory side effects are
the caller's responsibility. Allocation failure follows the process policy.

Copyable values support independent staging copies and `noexcept swap` for
atomic publication on the owning thread. Move-only values remain supported;
a moved-from storage is empty and keeps its configured limit. Mutation needs
one owning thread or external synchronization. Traversal visits live slots in
index order; erasing the current entry is allowed if the callback stops using
its value afterwards. Insertion/swap during traversal is unsupported.


Keys are local to a storage identity space, not global capabilities. A staging
copy intentionally contains the same keys with independently copied values;
swap publishes that complete state. Reusing a moved-from source starts a new
local identity space and may produce the same numeric keys as the moved-to
storage. An owning registry supplies its own owner/lineage identity when keys
must be distinguished across containers. Copying an old state back is an
explicit identity-state replacement, not a guarantee against stale keys across
arbitrary rollback or unrelated storages.

`SlotStorage::insert` forwards its argument to the final value: a `const T&`
needs one copy, and a `T` rvalue needs one move. Fresh slots construct their
optional value directly, with no intermediate `T` parameter or temporary
`Slot`. `try_insert` likewise moves a successful prepared value once into its
slot; any construction/moves performed by the factory itself belong to the
factory. Relocation of other values during vector growth is separate. The
legacy `SlotMap::insert(T)` signature is preserved; its prvalue parameter is
constructed directly and then moved once to the final slot.
