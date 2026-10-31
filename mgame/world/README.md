# mgame.world

Entities and their components as one block of plain data ([S4 design](../../.agents/docs/2026-10-29-mgame-s4-breakout-design.md) G-1).

```cpp
import mgame.world;

mgame::World<256, Position, Velocity, Brick> world;
const mgame::Entity e = world.create();          // the lowest free index
world.add(e, Position{100, 40});
world.each<Position, Velocity>([](mgame::Entity, Position& p, const Velocity& v) { p.x += v.x; });   // by index
const auto snapshot = world;                     // rollback, rewind: one copy
```

- **Columns:** every component type has a column of fixed length. A bit says whether each entity has that component. Components are plain data with no padding.
- **Entities:** an index and a generation. A destroyed entity's index is taken by a later one with the next generation, so its old handle no longer reaches anything.
- **Determinism:** a new entity takes the lowest free index, and `each` goes by index. The same calls make the same world everywhere, and `checksum` (the bytes' hash) says so.
- **Snapshots:** the world is trivially copyable, so saving it is a copy and loading is an assignment. This is why it is not EnTT or flecs: their registries live on the heap and are copied piece by piece.

The standard library alone.
