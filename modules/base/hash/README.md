# mlibs.hash

FNV-1a, once for the whole repository: the determinism hashes tests pin, content hashes (a display list, a surface, a frame's inputs), cache keys and generated ids. An independent component (D-55): the standard library only.

```cpp
import mlibs.hash;

mlibs::hash::Fnv1a h;
h.text("frame").u64(17).f32(0.5f);
std::println("{}", h.hex());                       // what a test pins
const auto key = mlibs::hash::fnv1a(path);            // a stable key from a name
const auto id = mlibs::hash::fnv1a32("file.cpp:12");  // a short generated id
```

- **FNV-1a over bytes** (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`) is what every pinned hash in openmui is: the same value on every platform and compiler. `byte`, `bytes`, `text`, `str` (the bytes and the length, one string of a sequence), `u64`, `i64`, `f64`, `f32` (its bits widened to eight bytes) and `le` (a value's low bytes): numbers go in as little-endian bytes, so a hash never depends on the machine's byte order.
- **Two other ways in, named apart:** `word` folds a whole value in with one multiply (quicker, for keys kept in memory and running checksums; not the FNV-1a of its bytes), and `object` hashes an object's own bytes (for types without padding).
- **FNV-1a 32** (`Fnv1a32`, `fnv1a32`) for short generated ids.

FNV's own reference vectors are checked at compile time (`static_assert`) and in `tests/hash_test.cpp`, with each way a value goes in written out by hand. `mlibs.testing` hashes with this package, so those tests use plain checks.

A package does not write its own FNV-1a: the lint rule `hash` points it here.

The package and public import are `mlibs.hash`. The legacy
`import openmui.hash;` and `omui::hash` API remain source-compatible
within this same package; rebuild C++ consumers. Both entry points share the
same types, implementation and state. No extra compatibility package is needed.
See the [namespace migration guide](../../../.agents/docs/2026-11-05-product-namespace-migration-design.md#5-n1首组五个基础库的实际迁移).
