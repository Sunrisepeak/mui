import std;
import mlibs.memory;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
struct Box {
    float x, y, w, h;
};
struct NodeTag;
}  // namespace

UTest cases{"memory", [] {

    "a frame of the same shape allocates nothing new"_test = [] {
        mlibs::memory::Arena arena(4096);
        for (int frame = 0; frame < 10; ++frame) {
            for (int i = 0; i < 100; ++i) (void)arena.make<Box>(1.0f, 2.0f, 3.0f, 4.0f);
            (void)arena.copy(std::string_view("hello"));
            arena.reset();
        }
        auto after_first = arena.heap_allocations();
        for (int frame = 0; frame < 100; ++frame) {
            for (int i = 0; i < 100; ++i) (void)arena.make<Box>();
            arena.reset();
        }
        expect(eq(arena.heap_allocations(), after_first));
        auto big = arena.array<float>(100000);
        expect(big.size() == 100000);
    };

    "large copies among small ones: the same sizes in any order allocate nothing new"_test = [] {
        // A renderer's frame: small commands, and the pixels of images of
        // several sizes uploaded among them, in an order that changes.
        mlibs::memory::Arena arena(16 * 1024);
        std::vector<std::byte> pixels(5 * 1024 * 1024, std::byte{7});
        std::array<std::size_t, 8> sizes{5u << 20, 1u << 20, 300u << 10, 2u << 20, 1u << 20, 64u << 10, 5u << 20, 700u << 10};
        auto frame = [&] {
            for (std::size_t size : sizes) {
                for (int k = 0; k < 50; ++k) (void)arena.make<Box>();
                const auto copy = arena.copy(std::span<const std::byte>(pixels).first(size));
                expect(copy.front() == std::byte{7} && copy.back() == std::byte{7});
            }
            arena.reset();
        };
        frame();
        const auto settled = arena.heap_allocations();
        const auto capacity = arena.capacity();
        std::minstd_rand order(42);
        for (int n = 0; n < 200; ++n) {
            std::ranges::shuffle(sizes, order);
            frame();
        }
        expect(eq(arena.heap_allocations(), settled));
        expect(eq(arena.capacity(), capacity));
    };

    "slot maps detect stale handles"_test = [] {
        mlibs::memory::SlotMap<std::string, NodeTag> map;
        auto a = map.insert("a");
        auto b = map.insert("b");
        expect(map.get(a) && *map.get(a) == "a");
        expect(map.erase(a));
        expect(map.get(a) == nullptr) << "a stale handle finds nothing";
        auto c = map.insert("c");
        expect(c.index == a.index && c.generation != a.generation) << "the slot is reused under a new generation";
        expect(map.get(a) == nullptr && *map.get(c) == "c");
        expect(eq(map.size(), std::size_t{2}));
        auto bits = b.bits();
        expect(mlibs::memory::Handle<NodeTag>::from_bits(bits) == b);
        int n = 0;
        map.for_each([&](auto, std::string&) { ++n; });
        expect(eq(n, 2));
        map.clear();
        expect(map.empty() && !map.contains(b));
    };

    "function references call without owning"_test = [] {
        int total = 0;
        auto add = [&](int v) { total += v; };
        mlibs::memory::FunctionRef<void(int)> f = add;
        f(2);
        f(3);
        expect(eq(total, 5));
        auto twice = [](mlibs::memory::FunctionRef<int(int)> g) { return g(g(1)); };
        expect(eq(twice([](int v) { return v * 3; }), 9));
    };

    "an owned handle names its registry: two registries' handles never meet, a stale one is told"_test = [] {
        struct NodeTag;
        using Ref = mlibs::memory::OwnedHandle<NodeTag>;
        const Ref a{1, 4, 7}, b{2, 4, 7}, stale{1, 4, 6};
        expect(bool(a) && !bool(Ref{})) << "an empty handle is false";
        expect(!bool(Ref{0, 4, 7}) && !bool(Ref{1, Ref::invalid_slot, 7}) && !bool(Ref{1, 4, 0})) << "no zero owner, slot or generation is issued";
        expect(a != b) << "the same slot and generation in another registry is another object";
        expect(a != stale && a == Ref{1, 4, 7}) << "a generation tells a stale handle";
        static_assert(!std::is_same_v<Ref, mlibs::memory::OwnedHandle<struct OtherTag>>, "tags keep domains apart");
    };
}};
