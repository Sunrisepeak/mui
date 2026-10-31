// The world: entities made at the lowest free index and their old handles
// refused after they are gone; components added, found and removed;
// iteration by index over those with every queried component; a copy is a
// snapshot (the same checksum, and it does not change with the original);
// a world built by the same calls has the same checksum everywhere.
import std;
import mgame.world;
import openmui.testing;

using namespace omui::testing;

namespace {

struct Position {
    std::int32_t x = 0, y = 0;
};
struct Velocity {
    std::int32_t x = 0, y = 0;
};
struct Tag {
    std::uint32_t kind = 0;
};
struct Spot {  // doubles: no padding, vouched for
    double x = 0, y = 0;
    static constexpr bool no_padding = true;
};
struct Padded {  // seven bytes of padding after the char
    char   c = 0;
    double d = 0;
};
static_assert(mgame::Component<Spot> && !mgame::Component<Padded>, "doubles by declaration; padding never");

using W = mgame::World<100, Position, Velocity, Tag>;

}  // namespace

int main(int argc, char** argv) {
    Suite s{"mgame.world"};

    s.test("entities: the lowest free index; a destroyed one's handle no longer reaches its successor", [] {
        W w;
        const auto a = w.create(), b = w.create(), c = w.create();
        check(a.index == 0 && b.index == 1 && c.index == 2 && w.count() == 3);
        w.destroy(b);
        check(!w.alive(b) && w.count() == 2);
        const auto d = w.create();
        check(d.index == 1 && d.generation == b.generation + 1 && w.alive(d) && !w.alive(b), "index 1 again, a new generation");
        W full;
        for (int i = 0; i < 100; ++i) check(full.create().valid());
        check(!full.create().valid(), "past capacity: none");
    });

    s.test("components: added, found, removed; gone with their entity", [] {
        W w;
        const auto e = w.create();
        w.add(e, Position{3, 4});
        check(w.has<Position>(e) && !w.has<Velocity>(e) && w.find<Position>(e)->x == 3);
        w.get<Position>(e).y = 9;
        check(w.find<Position>(e)->y == 9);
        w.remove<Position>(e);
        check(!w.has<Position>(e) && w.find<Position>(e) == nullptr);
        w.add(e, Velocity{1, 1});
        w.destroy(e);
        const auto f = w.create();
        check(f.index == e.index && !w.has<Velocity>(f), "a new entity at the index has none of the old one's");
    });

    s.test("a stale handle changes nothing: adding or removing through it leaves the entity now at its index alone", [] {
        W w;
        const auto e = w.create();
        w.destroy(e);
        const auto f = w.create();  // the same index, the next generation
        check(w.add(f, Position{1, 2}) != nullptr, "the living one takes it");
        check(w.add(e, Position{7, 7}) == nullptr && w.find<Position>(f)->x == 1, "the stale handle's add: refused, the slot untouched");
        check(!w.remove<Position>(e) && w.has<Position>(f), "the stale handle's remove: refused");
        check(w.remove<Position>(f) && !w.has<Position>(f), "the living one's remove");
    });

    s.test("iteration by index over those with every queried component", [] {
        W w;
        std::vector<mgame::Entity> made;
        for (int i = 0; i < 70; ++i) {
            const auto e = w.create();
            made.push_back(e);
            w.add(e, Position{i, 0});
            if (i % 3 == 0) w.add(e, Velocity{1, 2});
        }
        std::vector<std::uint32_t> seen;
        w.each<Position, Velocity>([&](mgame::Entity e, Position& p, Velocity& v) {
            p.x += v.x;
            seen.push_back(e.index);
        });
        check(seen.size() == 24 && std::ranges::is_sorted(seen) && seen.front() == 0 && seen.back() == 69, std::format("{} moved", seen.size()));
        check(w.get<Position>(made[3]).x == 4 && w.get<Position>(made[4]).x == 4);
        int all = 0;
        std::as_const(w).each<Position>([&](mgame::Entity, const Position&) { ++all; });
        check_eq(all, 70);
    });

    s.test("a copy is a snapshot; the same calls make the same checksum everywhere", [] {
        static_assert(std::is_trivially_copyable_v<W>, "a world is copied as bytes");
        W w;
        for (int i = 0; i < 40; ++i) {
            const auto e = w.create();
            w.add(e, Position{i * 7, -i});
            if (i % 2) w.add(e, Tag{static_cast<std::uint32_t>(i)});
            if (i % 5 == 0) w.destroy(e);
        }
        const W saved = w;
        check_eq(saved.checksum(), w.checksum());
        w.each<Position>([](mgame::Entity, Position& p) { ++p.x; });
        check(saved.checksum() != w.checksum(), "the snapshot is its own");
        w = saved;
        check_eq(w.checksum(), saved.checksum());
        std::println("    world {:016x}", saved.checksum());
        check_eq(saved.checksum(), std::uint64_t{0xf89125d8563a9df6});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
