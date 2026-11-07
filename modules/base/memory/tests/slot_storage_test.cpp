// Public lifecycle contracts, shared by legacy handles and independent owners.
import std;
import mlibs.memory;
import mlibs.testing;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::Handle;
using mlibs::memory::SlotKey;
using mlibs::memory::SlotMap;
using mlibs::memory::SlotStorage;
using mlibs::memory::SlotStorageError;
}

using namespace omui;
using namespace mlibs::testing;
namespace { std::atomic<std::size_t> allocations{0}; }
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* p = std::malloc(size ? size : 1)) return p;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
struct Tag;
using Key = SlotKey<Tag>;
enum class Error { capacity, rejected };
struct MoveCount {
    inline static unsigned moves = 0, copies = 0;
    int value;
    explicit MoveCount(int n) : value(n) {}
    MoveCount(const MoveCount& other) : value(other.value) { ++copies; }
    MoveCount(MoveCount&& other) noexcept : value(std::exchange(other.value, -1)) { ++moves; }
    static void reset() { moves = copies = 0; }
};
struct MoveOnlyCount {
    inline static unsigned moves = 0;
    std::unique_ptr<int> value;
    explicit MoveOnlyCount(int n) : value(std::make_unique<int>(n)) {}
    MoveOnlyCount(const MoveOnlyCount&) = delete;
    MoveOnlyCount(MoveOnlyCount&& other) noexcept : value(std::move(other.value)) { ++moves; }
};
static_assert(sizeof(Handle<Tag>) == 8);
static_assert(std::is_standard_layout_v<Handle<Tag>>);
static_assert(Handle<Tag>{0x89abcdefu, 0x12345678u}.bits() == 0x1234567889abcdefull);
static_assert(Handle<Tag>::from_bits(0xfedcba9876543210ull) == Handle<Tag>{0x76543210u, 0xfedcba98u});
static_assert(Handle<Tag>{}.bits() == 0x00000000ffffffffull);
static_assert(std::same_as<decltype(Key{}.index), std::uint32_t>);
static_assert(std::same_as<decltype(Key{}.generation), std::uint64_t>);
static_assert(noexcept(std::declval<SlotStorage<int>&>().swap(std::declval<SlotStorage<int>&>())));
}

int main(int argc, char** argv) {
    Suite suite{"memory.slot-storage"};
    suite.test("legacy ABI first generation and LIFO reuse retain their public behavior", [] {
        SlotMap<std::string, Tag> map;
        auto a = map.insert("first"), b = map.insert("second");
        check(a == Handle<Tag>{0, 0} && b == Handle<Tag>{1, 0});
        check_eq(a.bits(), std::uint64_t{0}); check_eq(b.bits(), std::uint64_t{1});
        check(map.erase(a) && map.erase(b));
        auto c = map.insert("third"), d = map.insert("fourth");
        check(c == Handle<Tag>{1, 1} && d == Handle<Tag>{0, 1});
        check(!map.contains(a) && !map.contains(b));
        const auto& read = map;
        std::string names;
        read.for_each([&](Handle<Tag>, const std::string& value) { names += value; });
        check_eq(names, std::string("fourththird"));
        map.clear(); check(!map.contains(c) && !map.contains(d));
        check(map.insert("after clear") == Handle<Tag>{1, 2});
    });
    suite.test("limits are lazy and include zero without allocating maximum capacity", [] {
        const auto before = allocations.load(std::memory_order_relaxed);
        SlotStorage<int, Key> large(1'000'000), zero(0);
        const auto after = allocations.load(std::memory_order_relaxed);
        check_eq(after, before);
        check_eq(large.slot_count(), std::size_t{0}); check_eq(large.max_slots(), std::size_t{1'000'000});
        const auto full = zero.insert(4);
        check(!zero.next_handle() && !full && full.error() == SlotStorageError::capacity);
        SlotStorage<int, Key> addressable(std::numeric_limits<std::size_t>::max());
        check_eq(addressable.max_slots(), static_cast<std::size_t>(Key::invalid_index));
    });
    suite.test("fresh factory failure leaves the exact candidate and allocation state unchanged", [] {
        SlotStorage<int, Key> storage(1);
        const auto candidate = storage.next_handle();
        int calls = 0; Key seen;
        const auto before = allocations.load(std::memory_order_relaxed);
        const auto rejected = storage.try_insert([&](Key key) -> std::expected<int, Error> {
            ++calls; seen = key;
            return std::unexpected(Error::rejected);
        }, Error::capacity);
        const auto after = allocations.load(std::memory_order_relaxed);
        check(!rejected && rejected.error() == Error::rejected);
        check_eq(calls, 1); check(seen == Key{0, 1} && candidate == seen);
        check(storage.next_handle() == candidate && storage.empty());
        check_eq(storage.slot_count(), std::size_t{0}); check_eq(storage.retired_count(), std::size_t{0});
        check_eq(storage.max_slots(), std::size_t{1}); check_eq(after, before);
        const auto accepted = storage.try_insert([](Key key) -> std::expected<int, Error> { return static_cast<int>(key.index) + 42; }, Error::capacity);
        check(accepted && *accepted == seen && *storage.get(*accepted) == 42);
    });
    suite.test("recycled factory failure preserves live neighbors and does not consume generation", [] {
        SlotStorage<std::string, Key> storage(2);
        auto a = storage.insert("old").value(), b = storage.insert("untouched").value();
        check(storage.erase(a));
        const auto candidate = storage.next_handle();
        const auto pointer = storage.get(b);
        for (int attempt = 0; attempt < 10; ++attempt) {
            auto failed = storage.try_insert([&](Key key) -> std::expected<std::string, Error> {
                check(key == Key{a.index, 2});
                check(storage.size() == 1 && storage.contains(b)); // reads during preparation are allowed
                return std::unexpected(Error::rejected);
            }, Error::capacity);
            check(!failed && failed.error() == Error::rejected && storage.next_handle() == candidate);
        }
        check(storage.get(b) == pointer && *pointer == "untouched");
        check_eq(storage.slot_count(), std::size_t{2}); check_eq(storage.size(), std::size_t{1});
        auto c = storage.insert("new").value();
        check(c == Key{a.index, 2} && !storage.contains(a));
    });
    suite.test("full storage rejects before invoking the factory or changing a live value", [] {
        SlotStorage<int, Key> storage(1);
        const auto first = storage.insert(7).value();
        int calls = 0;
        auto result = storage.try_insert([&](Key) -> std::expected<int, Error> { ++calls; return 8; }, Error::capacity);
        check(!result && result.error() == Error::capacity && calls == 0);
        check(storage.contains(first) && *storage.get(first) == 7 && storage.slot_count() == 1);
        check(storage.erase(first));
        check(storage.insert(9).value() == Key{0, 2});
    });
    suite.test("uint8 exhaustion retires the slot and the capacity cannot be bypassed", [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny> storage(1);
        std::array<Tiny, 255> historical{};
        for (unsigned generation = 1; generation <= 255; ++generation) {
            auto key = storage.insert(static_cast<int>(generation));
            check(key.has_value());
            if (!key) return;
            historical[generation - 1] = *key;
            check(key->index == 0 && key->generation == generation);
            check(storage.erase(*key)); check(!storage.erase(*key));
        }
        check(storage.empty() && storage.slot_count() == 1 && storage.retired_count() == 1);
        check(!storage.next_handle());
        for (auto key : historical) check(!storage.contains(key) && storage.get(key) == nullptr);
        int calls = 0;
        auto full = storage.try_insert([&](Tiny) -> std::expected<int, Error> { ++calls; return 3; }, Error::capacity);
        check(!full && full.error() == Error::capacity && calls == 0);
        storage.clear();
        check(!storage.next_handle() && storage.retired_count() == 1 && storage.slot_count() == 1);
    });
    suite.test("a retired slot counts toward the limit while a vacant slot remains reusable", [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 255> storage(2);
        const auto a = storage.insert(1).value(); check(storage.erase(a));
        const auto b = storage.insert(2).value();
        check(b.index == 1 && b.generation == 255 && storage.retired_count() == 1);
        check(storage.erase(b));
        check(storage.slot_count() == 2 && storage.retired_count() == 2 && !storage.insert(3));
    });
    suite.test("clear preserves retirement and bumps only live entries once in legacy index order", [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 254> storage(3);
        const auto a = storage.insert(1).value(), b = storage.insert(2).value(), c = storage.insert(3).value();
        check(storage.erase(a)); const auto last_a = storage.insert(4).value(); check(storage.erase(last_a));
        storage.clear();
        const auto candidate = storage.next_handle();
        check(candidate == Tiny{2, 255});
        storage.clear(); check(storage.next_handle() == candidate);
        check(!storage.contains(b) && !storage.contains(c) && storage.retired_count() == 1);
        const auto last_c = storage.insert(5).value(), last_b = storage.insert(6).value();
        check(last_c == Tiny{2, 255} && last_b == Tiny{1, 255});
        storage.clear(); check(storage.retired_count() == 3 && storage.slot_count() == 3 && !storage.next_handle());
    });
    suite.test("copy staging isolates edits and noexcept swap publishes the complete identity state", [] {
        SlotStorage<std::string, Key> original(3);
        const auto a = original.insert("a").value(), b = original.insert("b").value();
        const auto pointer = original.get(a);
        auto staging = original;
        check(staging.erase(a));
        const auto c = staging.insert("c").value();
        check(original.contains(a) && !original.contains(c) && original.get(a) == pointer && *pointer == "a");
        check(staging.contains(c) && !staging.contains(a) && *staging.get(b) == "b");
        original.swap(staging);
        check(original.contains(c) && !original.contains(a) && *original.get(c) == "c");
        check(staging.contains(a) && staging.get(a) == pointer && *pointer == "a");
        SlotStorage<std::string, Key> replacement(0);
        replacement = original;
        check(replacement.max_slots() == 3 && replacement.contains(c));
        check(replacement.erase(c) && original.contains(c));
    });
    suite.test("swap transports quotas free lists and retirement as one state", [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 255> retired(1), live(2);
        const auto dead = retired.insert(1).value(); check(retired.erase(dead));
        const auto current = live.insert(2).value();
        swap(retired, live);
        check(retired.max_slots() == 2 && retired.retired_count() == 0 && retired.contains(current));
        check(live.max_slots() == 1 && live.retired_count() == 1 && !live.next_handle());
    });
    suite.test("move-only values work with factories and moved-from containers are empty reusable states", [] {
        using Storage = SlotStorage<std::unique_ptr<int>, Key>;
        static_assert(!std::is_copy_constructible_v<Storage>);
        static_assert(std::is_nothrow_move_constructible_v<Storage>);
        Storage source(2);
        const auto key = source.try_insert([](Key) -> std::expected<std::unique_ptr<int>, Error> { return std::make_unique<int>(23); }, Error::capacity).value();
        const auto payload = source.get(key)->get();
        Storage destination(std::move(source));
        check(source.empty() && source.slot_count() == 0 && source.max_slots() == 2);
        check(destination.contains(key) && destination.get(key)->get() == payload && **destination.get(key) == 23);
        check(source.insert(std::make_unique<int>(99)).has_value());
        Storage assigned(0); assigned = std::move(destination);
        check(destination.empty() && destination.slot_count() == 0 && destination.max_slots() == 2);
        check(assigned.contains(key) && **assigned.get(key) == 23);
    });
    suite.test("move contract: legacy fresh and recycled prvalues need one move", [] {
        SlotMap<MoveCount, Tag> legacy;
        MoveCount::reset();
        const auto first = legacy.insert(MoveCount{7});
        check(legacy.get(first) && legacy.get(first)->value == 7);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
        check(legacy.erase(first));
        MoveCount::reset();
        const auto recycled = legacy.insert(MoveCount{9});
        check(recycled.index == first.index && legacy.get(recycled)->value == 9);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
    });
    suite.test("move contract: shared const values copy once and rvalues move once", [] {
        SlotStorage<MoveCount, Key> storage(1);
        const MoveCount constant(23);
        MoveCount::reset();
        const auto copied = storage.insert(constant).value();
        check(storage.get(copied)->value == 23 && constant.value == 23);
        check_eq(MoveCount::copies, 1u); check_eq(MoveCount::moves, 0u);
        check(storage.erase(copied));
        MoveCount movable(41);
        MoveCount::reset();
        const auto moved = storage.insert(std::move(movable)).value();
        check(storage.get(moved)->value == 41 && movable.value == -1);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
        check(storage.erase(moved));
        MoveCount::reset();
        const auto temporary = storage.insert(MoveCount{53}).value();
        check(storage.get(temporary)->value == 53);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
    });
    suite.test("move contract: factory expected in-place reaches fresh and recycled slots with one value move", [] {
        SlotStorage<MoveCount, Key> storage(1);
        auto prepare = [](Key) -> std::expected<MoveCount, Error> { return std::expected<MoveCount, Error>(std::in_place, 71); };
        MoveCount::reset();
        const auto fresh = storage.try_insert(prepare, Error::capacity).value();
        check(storage.get(fresh)->value == 71);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
        check(storage.erase(fresh));
        MoveCount::reset();
        const auto reused = storage.try_insert(prepare, Error::capacity).value();
        check(storage.get(reused)->value == 71 && reused.index == fresh.index);
        check_eq(MoveCount::copies, 0u); check_eq(MoveCount::moves, 1u);
    });
    suite.test("move contract: move-only resources have no intermediate insertion value", [] {
        SlotStorage<MoveOnlyCount, Key> storage(1);
        static_assert(!std::is_copy_constructible_v<decltype(storage)>);
        MoveOnlyCount::moves = 0;
        const auto fresh = storage.insert(MoveOnlyCount{97}).value();
        check(*storage.get(fresh)->value == 97);
        check_eq(MoveOnlyCount::moves, 1u);
        check(storage.erase(fresh));
        MoveOnlyCount value(101);
        const auto pointer = value.value.get();
        MoveOnlyCount::moves = 0;
        const auto reused = storage.insert(std::move(value)).value();
        check(storage.get(reused)->value.get() == pointer && !value.value);
        check_eq(MoveOnlyCount::moves, 1u);
    });
    suite.test("mutable and const queries and traversal perform no allocation", [] {
        SlotStorage<int, Key> storage(100);
        std::array<Key, 100> keys{};
        for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = storage.insert(static_cast<int>(i)).value();
        for (std::size_t i = 0; i < keys.size(); i += 3) check(storage.erase(keys[i]));
        const auto before = allocations.load(std::memory_order_relaxed);
        const auto& read = storage;
        std::uint64_t sum = 0; bool valid = true;
        for (unsigned repeat = 0; repeat < 100; ++repeat) {
            for (auto key : keys) {
                valid &= storage.contains(key) == (read.get(key) != nullptr);
                valid &= storage.get(key) == read.get(key);
            }
            storage.for_each([&](Key key, int& value) { valid &= read.contains(key); ++value; });
            read.for_each([&](Key, const int& value) { sum += static_cast<unsigned>(value); });
            valid &= read.next_handle().has_value() && read.slot_count() == 100 && read.size() == 66;
            valid &= !read.contains(Key{}) && read.get(Key{}) == nullptr;
        }
        const auto after = allocations.load(std::memory_order_relaxed);
        check(valid && sum > 0); check_eq(after, before);
    });
    return suite.run(argc, argv);
}
