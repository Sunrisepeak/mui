# xengine.world

Entities and their components as one block of plain data ([S4 design](../../../../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) G-1).

```cpp
import xengine.world;

xengine::World<256, Position, Velocity, Brick> world;
const xengine::Entity e = world.create();          // the lowest free index
world.add(e, Position{100, 40});
world.each<Position, Velocity>([](xengine::Entity, Position& p, const Velocity& v) { p.x += v.x; });   // by index
const auto snapshot = world;                     // rollback, rewind: one copy
```

- **Columns:** every component type has a column of fixed length. A bit says whether each entity has that component. Components are plain data with no padding.
- **Entities:** an index and a generation. A destroyed entity's index is taken by a later one with the next generation, so its old handle no longer reaches anything.
- **Determinism:** a new entity takes the lowest free index, and `each` goes by index. The same calls make the same world everywhere, and `checksum` (the bytes' hash) says so.
- **Snapshots:** the world is trivially copyable, so saving it is a copy and loading is an assignment. This is why it is not EnTT or flecs: their registries live on the heap and are copied piece by piece.

## A world too large to copy whole: `Chunked`

```cpp
xengine::Chunked<Position, Velocity, Health> world;
const auto hero = world.create();                   // 1, 2, 3 …: never taken again
world.add(hero, Position{0, 0}), world.add(hero, Velocity{1, 0});
world.each<Position, const Velocity>([](auto, Position& at, const Velocity& by) { at.x += by.x; });
const auto branch = world;                          // a pointer a chunk
world.changed_since(branch);                        // chunks and directory pages each holds alone
```

- **Archetypes in chunks:** entities with the same components are an archetype, kept in chunks of 64, each component's values side by side. Adding or taking a component moves the entity to its new archetype; the last row of the old one takes its place.
- **A copy is a branch** (D-169): chunks and the directory's pages are shared between copies until one side writes, which copies that chunk (or page) alone. A query that only reads (`const` components) copies nothing. 100,000 entities are 1,565 chunks: a copy is 1,565 pointers, and after one write 1 chunk is the copy's own.
- **Comparing branches:** `changed_since` says which chunks and pages two copies of one world do not share, without looking at a value — what an agent compares first.
- **Determinism:** ids are given in order and never taken again; every move follows the order of the calls; `checksum` hashes every entity's id and components in the world's order.
- **Against the strongest:** flecs and EnTT keep archetypes or sparse sets on the heap, copied piece by piece; Unity's DOTS keeps 16 KB chunks but copies a world whole. Here a copy shares what it has not written, so a snapshot or branch costs a pointer a chunk. Behind: no parallel `each`, no relations or queries cached across calls.

The standard library and `mlibs.hash` (in `mlibs.utils`).
