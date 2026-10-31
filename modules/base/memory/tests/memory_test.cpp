import std;
import openmui.memory;
import openmui.testing;

using namespace omui::testing;

namespace {
struct Box {
    float x, y, w, h;
};
struct NodeTag;
}  // namespace

int main(int argc, char** argv) {
    Suite s{"memory"};

    s.test("a frame of the same shape allocates nothing new", [] {
        omui::Arena arena(4096);
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
        check_eq(arena.heap_allocations(), after_first);
        auto big = arena.array<float>(100000);
        check(big.size() == 100000);
    });

    s.test("large copies among small ones: the same sizes in any order allocate nothing new", [] {
        // A renderer's frame: small commands, and the pixels of images of
        // several sizes uploaded among them, in an order that changes.
        omui::Arena arena(16 * 1024);
        std::vector<std::byte> pixels(5 * 1024 * 1024, std::byte{7});
        std::array<std::size_t, 8> sizes{5u << 20, 1u << 20, 300u << 10, 2u << 20, 1u << 20, 64u << 10, 5u << 20, 700u << 10};
        auto frame = [&] {
            for (std::size_t size : sizes) {
                for (int k = 0; k < 50; ++k) (void)arena.make<Box>();
                const auto copy = arena.copy(std::span<const std::byte>(pixels).first(size));
                check(copy.front() == std::byte{7} && copy.back() == std::byte{7});
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
        check_eq(arena.heap_allocations(), settled);
        check_eq(arena.capacity(), capacity);
    });

    s.test("slot maps detect stale handles", [] {
        omui::SlotMap<std::string, NodeTag> map;
        auto a = map.insert("a");
        auto b = map.insert("b");
        check(map.get(a) && *map.get(a) == "a");
        check(map.erase(a));
        check(map.get(a) == nullptr, "a stale handle finds nothing");
        auto c = map.insert("c");
        check(c.index == a.index && c.generation != a.generation, "the slot is reused under a new generation");
        check(map.get(a) == nullptr && *map.get(c) == "c");
        check_eq(map.size(), std::size_t{2});
        auto bits = b.bits();
        check(omui::Handle<NodeTag>::from_bits(bits) == b);
        int n = 0;
        map.for_each([&](auto, std::string&) { ++n; });
        check_eq(n, 2);
        map.clear();
        check(map.empty() && !map.contains(b));
    });

    s.test("function references call without owning", [] {
        int total = 0;
        auto add = [&](int v) { total += v; };
        omui::FunctionRef<void(int)> f = add;
        f(2);
        f(3);
        check_eq(total, 5);
        auto twice = [](omui::FunctionRef<int(int)> g) { return g(g(1)); };
        check_eq(twice([](int v) { return v * 3; }), 9);
    });

    return s.run(argc, argv);
}
