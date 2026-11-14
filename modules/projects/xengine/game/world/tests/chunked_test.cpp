// The chunked world: entities made, given and stripped of components with their values kept across archetypes;
// gone ones refused; iteration over those with every queried component; a copy shares every chunk until one side
// writes, and then only the chunk written is its own; the same calls make the same checksum.
import std;
import xengine.world;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {

struct Position {
    std::int32_t x = 0, y = 0;
};
struct Velocity {
    std::int32_t x = 0, y = 0;
};
struct Health {
    std::uint32_t points = 0;
};
using World = xengine::Chunked<Position, Velocity, Health>;

// A world of `count` entities, every third without a velocity, every fifth with health.
World made(std::uint32_t count) {
    World world;
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto id = world.create();
        world.add(id, Position{static_cast<std::int32_t>(index), 0});
        if (index % 3) world.add(id, Velocity{1, 2});
        if (index % 5 == 0) world.add(id, Health{index});
    }
    return world;
}

}  // namespace

UTest cases{"xengine.world.chunked", [] {
    "components added, kept across archetypes, removed; a gone entity refused and its id never taken again"_test = [] {
        World world;
        const auto first = world.create(), second = world.create();
        expect(eq(first, World::Id{1}) && eq(second, World::Id{2}));
        expect(world.add(first, Position{3, 4}) && world.add(first, Velocity{5, 6}) && world.add(second, Position{7, 8}));
        expect(world.add(first, Health{9}));
        expect(eq(world.get<Position>(first)->x, 3) && eq(world.get<Velocity>(first)->y, 6) && eq(world.get<Health>(first)->points, 9u));
        expect(world.remove<Velocity>(first) && !world.has<Velocity>(first) && !world.remove<Velocity>(first));
        expect(eq(world.get<Position>(first)->y, 4) && eq(world.get<Health>(first)->points, 9u)) << "kept across the move";
        expect(world.add(first, Position{10, 11}) && eq(world.get<Position>(first)->x, 10)) << "set where it has one";
        expect(world.destroy(first) && !world.alive(first) && !world.destroy(first) && !world.add(first, Health{1}));
        expect(eq(world.get<Position>(second)->x, 7) && eq(world.size(), std::size_t{1}));
        expect(eq(world.create(), World::Id{3}));
        expect(!world.alive(0) && !world.alive(99) && world.get<Position>(99) == nullptr);
    };

    "the last row takes a removed one's place, and every entity is found where the directory says"_test = [] {
        World world = made(1000);
        for (World::Id id = 1; id <= 1000; id += 7) expect(world.destroy(id));
        for (World::Id id = 2; id <= 1000; id += 11) world.remove<Position>(id);
        std::size_t seen = 0, wrong = 0;
        world.each<const Position>([&](World::Id id, const Position& at) {
            ++seen;
            if (at.x != static_cast<std::int32_t>(id - 1) || !world.has<Position>(id)) ++wrong;
        });
        for (World::Id id = 1; id <= 1000; ++id) {
            const bool gone = (id - 1) % 7 == 0, taken = id >= 2 && (id - 2) % 11 == 0;
            if (world.alive(id) == gone) ++wrong;
            if (!gone && world.has<Position>(id) == taken) ++wrong;
        }
        expect(eq(wrong, std::size_t{0}));
        std::size_t moving = 0;
        world.each<Position, const Velocity>([&](World::Id, Position& at, const Velocity& by) { at.x += by.x, ++moving; });
        std::size_t expected = 0;
        for (World::Id id = 1; id <= 1000; ++id) expected += world.has<Position>(id) && world.has<Velocity>(id);
        expect(eq(moving, expected) && gt(seen, moving));
    };

    "a copy of 100,000 shares every chunk; one write makes one chunk and one page its own, and the copy keeps its value"_test = [] {
        World world = made(100000);
        const World branch = world;
        const auto before = world.changed_since(branch);
        expect(eq(before.chunks_own, std::size_t{0}) && eq(before.chunks_shared, world.chunks()));
        world.edit<Position>(50000)->x = -1;
        const auto after = world.changed_since(branch);
        std::println("100,000 entities in {} chunks: one write, {} chunk(s) and {} page(s) its own", world.chunks(), after.chunks_own, after.pages_own);
        expect(eq(after.chunks_own, std::size_t{1}) && eq(after.chunks_shared, world.chunks() - 1) && eq(after.pages_own, std::size_t{0}));
        expect(eq(branch.get<Position>(50000)->x, 49999) && eq(world.get<Position>(50000)->x, -1));
        expect(ne(world.checksum(), branch.checksum()));
        // A move between archetypes writes the directory: its page, and the page of the row that fills its place.
        world.remove<Position>(70000);
        const auto moved = world.changed_since(branch);
        expect(ge(moved.pages_own, std::size_t{1}) && le(moved.pages_own, std::size_t{2}) && le(moved.chunks_own, std::size_t{4}));
        // A query that only reads copies nothing.
        std::int64_t sum = 0;
        World reader = branch;
        reader.each<const Position>([&](World::Id, const Position& at) { sum += at.x; });
        expect(eq(reader.changed_since(branch).chunks_own, std::size_t{0}) && gt(sum, std::int64_t{0}));
    };

    "the same calls make the same checksum; a different order of the same entities does not"_test = [] {
        const World one = made(5000), other = made(5000);
        expect(eq(one.checksum(), other.checksum()));
        World swapped;
        const auto second = swapped.create(), first = swapped.create();
        swapped.add(first, Position{1, 0}), swapped.add(second, Position{2, 0});
        World ordered;
        const auto first_too = ordered.create(), second_too = ordered.create();
        ordered.add(first_too, Position{1, 0}), ordered.add(second_too, Position{2, 0});
        expect(ne(swapped.checksum(), ordered.checksum()));
    };
}};
