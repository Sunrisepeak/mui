// Public lifecycle contracts, shared by legacy handles and independent owners.
import std;
import mlibs.memory;
import mcpplibs.mtest;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::Handle;
using mlibs::memory::SlotKey;
using mlibs::memory::SlotMap;
using mlibs::memory::SlotStorage;
using mlibs::memory::SlotStorageError;
}

using namespace omui;
using namespace mcpplibs::mtest;
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

UTest cases{"memory.slot-storage", [] {
    "legacy ABI first generation and LIFO reuse retain their public behavior"_test = [] {
        SlotMap<std::string, Tag> map;
        auto a = map.insert("first"), b = map.insert("second");
        expect(a == Handle<Tag>{0, 0} && b == Handle<Tag>{1, 0});
        expect(eq(a.bits(), std::uint64_t{0})); expect(eq(b.bits(), std::uint64_t{1}));
        expect(map.erase(a) && map.erase(b));
        auto c = map.insert("third"), d = map.insert("fourth");
        expect(c == Handle<Tag>{1, 1} && d == Handle<Tag>{0, 1});
        expect(!map.contains(a) && !map.contains(b));
        const auto& read = map;
        std::string names;
        read.for_each([&](Handle<Tag>, const std::string& value) { names += value; });
        expect(eq(names, std::string("fourththird")));
        map.clear(); expect(!map.contains(c) && !map.contains(d));
        expect(map.insert("after clear") == Handle<Tag>{1, 2});
    };
    "limits are lazy and include zero without allocating maximum capacity"_test = [] {
        const auto before = allocations.load(std::memory_order_relaxed);
        SlotStorage<int, Key> large(1'000'000), zero(0);
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(eq(after, before));
        expect(eq(large.slot_count(), std::size_t{0})); expect(eq(large.max_slots(), std::size_t{1'000'000}));
        const auto full = zero.insert(4);
        expect(!zero.next_handle() && !full && full.error() == SlotStorageError::capacity);
        SlotStorage<int, Key> addressable(std::numeric_limits<std::size_t>::max());
        expect(eq(addressable.max_slots(), static_cast<std::size_t>(Key::invalid_index)));
    };
    "fresh factory failure leaves the exact candidate and allocation state unchanged"_test = [] {
        SlotStorage<int, Key> storage(1);
        const auto candidate = storage.next_handle();
        int calls = 0; Key seen;
        const auto before = allocations.load(std::memory_order_relaxed);
        const auto rejected = storage.try_insert([&](Key key) -> std::expected<int, Error> {
            ++calls; seen = key;
            return std::unexpected(Error::rejected);
        }, Error::capacity);
        const auto after = allocations.load(std::memory_order_relaxed);
        expect(!rejected && rejected.error() == Error::rejected);
        expect(eq(calls, 1)); expect(seen == Key{0, 1} && candidate == seen);
        expect(storage.next_handle() == candidate && storage.empty());
        expect(eq(storage.slot_count(), std::size_t{0})); expect(eq(storage.retired_count(), std::size_t{0}));
        expect(eq(storage.max_slots(), std::size_t{1})); expect(eq(after, before));
        const auto accepted = storage.try_insert([](Key key) -> std::expected<int, Error> { return static_cast<int>(key.index) + 42; }, Error::capacity);
        expect(accepted && *accepted == seen && *storage.get(*accepted) == 42);
    };
    "recycled factory failure preserves live neighbors and does not consume generation"_test = [] {
        SlotStorage<std::string, Key> storage(2);
        auto a = storage.insert("old").value(), b = storage.insert("untouched").value();
        expect(storage.erase(a));
        const auto candidate = storage.next_handle();
        const auto pointer = storage.get(b);
        for (int attempt = 0; attempt < 10; ++attempt) {
            auto failed = storage.try_insert([&](Key key) -> std::expected<std::string, Error> {
                expect(key == Key{a.index, 2});
                expect(storage.size() == 1 && storage.contains(b)); // reads during preparation are allowed
                return std::unexpected(Error::rejected);
            }, Error::capacity);
            expect(!failed && failed.error() == Error::rejected && storage.next_handle() == candidate);
        }
        expect(storage.get(b) == pointer && *pointer == "untouched");
        expect(eq(storage.slot_count(), std::size_t{2})); expect(eq(storage.size(), std::size_t{1}));
        auto c = storage.insert("new").value();
        expect(c == Key{a.index, 2} && !storage.contains(a));
    };
    "full storage rejects before invoking the factory or changing a live value"_test = [] {
        SlotStorage<int, Key> storage(1);
        const auto first = storage.insert(7).value();
        int calls = 0;
        auto result = storage.try_insert([&](Key) -> std::expected<int, Error> { ++calls; return 8; }, Error::capacity);
        expect(!result && result.error() == Error::capacity && calls == 0);
        expect(storage.contains(first) && *storage.get(first) == 7 && storage.slot_count() == 1);
        expect(storage.erase(first));
        expect(storage.insert(9).value() == Key{0, 2});
    };
    "uint8 exhaustion retires the slot and the capacity cannot be bypassed"_test = [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny> storage(1);
        std::array<Tiny, 255> historical{};
        for (unsigned generation = 1; generation <= 255; ++generation) {
            auto key = storage.insert(static_cast<int>(generation));
            expect(key.has_value());
            if (!key) return;
            historical[generation - 1] = *key;
            expect(key->index == 0 && key->generation == generation);
            expect(storage.erase(*key)); expect(!storage.erase(*key));
        }
        expect(storage.empty() && storage.slot_count() == 1 && storage.retired_count() == 1);
        expect(!storage.next_handle());
        for (auto key : historical) expect(!storage.contains(key) && storage.get(key) == nullptr);
        int calls = 0;
        auto full = storage.try_insert([&](Tiny) -> std::expected<int, Error> { ++calls; return 3; }, Error::capacity);
        expect(!full && full.error() == Error::capacity && calls == 0);
        storage.clear();
        expect(!storage.next_handle() && storage.retired_count() == 1 && storage.slot_count() == 1);
    };
    "a retired slot counts toward the limit while a vacant slot remains reusable"_test = [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 255> storage(2);
        const auto a = storage.insert(1).value(); expect(storage.erase(a));
        const auto b = storage.insert(2).value();
        expect(b.index == 1 && b.generation == 255 && storage.retired_count() == 1);
        expect(storage.erase(b));
        expect(storage.slot_count() == 2 && storage.retired_count() == 2 && !storage.insert(3));
    };
    "clear preserves retirement and bumps only live entries once in legacy index order"_test = [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 254> storage(3);
        const auto a = storage.insert(1).value(), b = storage.insert(2).value(), c = storage.insert(3).value();
        expect(storage.erase(a)); const auto last_a = storage.insert(4).value(); expect(storage.erase(last_a));
        storage.clear();
        const auto candidate = storage.next_handle();
        expect(candidate == Tiny{2, 255});
        storage.clear(); expect(storage.next_handle() == candidate);
        expect(!storage.contains(b) && !storage.contains(c) && storage.retired_count() == 1);
        const auto last_c = storage.insert(5).value(), last_b = storage.insert(6).value();
        expect(last_c == Tiny{2, 255} && last_b == Tiny{1, 255});
        storage.clear(); expect(storage.retired_count() == 3 && storage.slot_count() == 3 && !storage.next_handle());
    };
    "copy staging isolates edits and noexcept swap publishes the complete identity state"_test = [] {
        SlotStorage<std::string, Key> original(3);
        const auto a = original.insert("a").value(), b = original.insert("b").value();
        const auto pointer = original.get(a);
        auto staging = original;
        expect(staging.erase(a));
        const auto c = staging.insert("c").value();
        expect(original.contains(a) && !original.contains(c) && original.get(a) == pointer && *pointer == "a");
        expect(staging.contains(c) && !staging.contains(a) && *staging.get(b) == "b");
        original.swap(staging);
        expect(original.contains(c) && !original.contains(a) && *original.get(c) == "c");
        expect(staging.contains(a) && staging.get(a) == pointer && *pointer == "a");
        SlotStorage<std::string, Key> replacement(0);
        replacement = original;
        expect(replacement.max_slots() == 3 && replacement.contains(c));
        expect(replacement.erase(c) && original.contains(c));
    };
    "swap transports quotas free lists and retirement as one state"_test = [] {
        using Tiny = SlotKey<Tag, std::uint8_t>;
        SlotStorage<int, Tiny, 255> retired(1), live(2);
        const auto dead = retired.insert(1).value(); expect(retired.erase(dead));
        const auto current = live.insert(2).value();
        swap(retired, live);
        expect(retired.max_slots() == 2 && retired.retired_count() == 0 && retired.contains(current));
        expect(live.max_slots() == 1 && live.retired_count() == 1 && !live.next_handle());
    };
    "move-only values work with factories and moved-from containers are empty reusable states"_test = [] {
        using Storage = SlotStorage<std::unique_ptr<int>, Key>;
        static_assert(!std::is_copy_constructible_v<Storage>);
        static_assert(std::is_nothrow_move_constructible_v<Storage>);
        Storage source(2);
        const auto key = source.try_insert([](Key) -> std::expected<std::unique_ptr<int>, Error> { return std::make_unique<int>(23); }, Error::capacity).value();
        const auto payload = source.get(key)->get();
        Storage destination(std::move(source));
        expect(source.empty() && source.slot_count() == 0 && source.max_slots() == 2);
        expect(destination.contains(key) && destination.get(key)->get() == payload && **destination.get(key) == 23);
        expect(source.insert(std::make_unique<int>(99)).has_value());
        Storage assigned(0); assigned = std::move(destination);
        expect(destination.empty() && destination.slot_count() == 0 && destination.max_slots() == 2);
        expect(assigned.contains(key) && **assigned.get(key) == 23);
    };
    "move contract: legacy fresh and recycled prvalues need one move"_test = [] {
        SlotMap<MoveCount, Tag> legacy;
        MoveCount::reset();
        const auto first = legacy.insert(MoveCount{7});
        expect(legacy.get(first) && legacy.get(first)->value == 7);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
        expect(legacy.erase(first));
        MoveCount::reset();
        const auto recycled = legacy.insert(MoveCount{9});
        expect(recycled.index == first.index && legacy.get(recycled)->value == 9);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
    };
    "move contract: shared const values copy once and rvalues move once"_test = [] {
        SlotStorage<MoveCount, Key> storage(1);
        const MoveCount constant(23);
        MoveCount::reset();
        const auto copied = storage.insert(constant).value();
        expect(storage.get(copied)->value == 23 && constant.value == 23);
        expect(eq(MoveCount::copies, 1u)); expect(eq(MoveCount::moves, 0u));
        expect(storage.erase(copied));
        MoveCount movable(41);
        MoveCount::reset();
        const auto moved = storage.insert(std::move(movable)).value();
        expect(storage.get(moved)->value == 41 && movable.value == -1);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
        expect(storage.erase(moved));
        MoveCount::reset();
        const auto temporary = storage.insert(MoveCount{53}).value();
        expect(storage.get(temporary)->value == 53);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
    };
    "move contract: factory expected in-place reaches fresh and recycled slots with one value move"_test = [] {
        SlotStorage<MoveCount, Key> storage(1);
        auto prepare = [](Key) -> std::expected<MoveCount, Error> { return std::expected<MoveCount, Error>(std::in_place, 71); };
        MoveCount::reset();
        const auto fresh = storage.try_insert(prepare, Error::capacity).value();
        expect(storage.get(fresh)->value == 71);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
        expect(storage.erase(fresh));
        MoveCount::reset();
        const auto reused = storage.try_insert(prepare, Error::capacity).value();
        expect(storage.get(reused)->value == 71 && reused.index == fresh.index);
        expect(eq(MoveCount::copies, 0u)); expect(eq(MoveCount::moves, 1u));
    };
    "move contract: move-only resources have no intermediate insertion value"_test = [] {
        SlotStorage<MoveOnlyCount, Key> storage(1);
        static_assert(!std::is_copy_constructible_v<decltype(storage)>);
        MoveOnlyCount::moves = 0;
        const auto fresh = storage.insert(MoveOnlyCount{97}).value();
        expect(*storage.get(fresh)->value == 97);
        expect(eq(MoveOnlyCount::moves, 1u));
        expect(storage.erase(fresh));
        MoveOnlyCount value(101);
        const auto pointer = value.value.get();
        MoveOnlyCount::moves = 0;
        const auto reused = storage.insert(std::move(value)).value();
        expect(storage.get(reused)->value.get() == pointer && !value.value);
        expect(eq(MoveOnlyCount::moves, 1u));
    };
    "mutable and const queries and traversal perform no allocation"_test = [] {
        SlotStorage<int, Key> storage(100);
        std::array<Key, 100> keys{};
        for (std::size_t i = 0; i < keys.size(); ++i) keys[i] = storage.insert(static_cast<int>(i)).value();
        for (std::size_t i = 0; i < keys.size(); i += 3) expect(storage.erase(keys[i]));
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
        expect(valid && sum > 0); expect(eq(after, before));
    };
}};
