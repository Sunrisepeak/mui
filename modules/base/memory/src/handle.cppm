// mlibs.memory.api:handle — generational handles and the slot map behind them.
//
// Objects the runtime hands out (nodes, signals, subscriptions, images) are
// referred to by a handle: an index and a generation. A handle to an object
// that has gone is detected (its generation no longer matches) instead of
// reading freed memory, which is what lets a script or an agent hold a handle
// across frames safely.
export module mlibs.memory.api:handle;

import std;
import :slots;

export namespace mlibs::memory {

template <class Tag>
struct Handle {
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index      = invalid_index;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != invalid_index; }
    constexpr explicit operator bool() const noexcept { return valid(); }

    // As one 64-bit value, the form the C ABI uses (OmuiNode).
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept {
        return (static_cast<std::uint64_t>(generation) << 32) | index;
    }
    [[nodiscard]] static constexpr Handle from_bits(std::uint64_t b) noexcept {
        return {static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(b >> 32)};
    }

    friend constexpr bool operator==(Handle a, Handle b) noexcept {
        return a.index == b.index && a.generation == b.generation;
    }
};

// Compatibility adapter over the shared slot mechanism. The original
// index32/generation32 ABI and generation-zero first insertion are preserved.
// Exhaustion now safely retires the slot instead of wrapping to generation 0.
template <class T, class Tag>
class SlotMap {
public:
    using handle_type = Handle<Tag>;
    handle_type insert(T value) {
        auto result = storage_.insert(std::move(value));
        return result ? *result : handle_type{};
    }
    bool erase(handle_type key) { return storage_.erase(key); }
    [[nodiscard]] bool contains(handle_type key) const noexcept { return storage_.contains(key); }
    [[nodiscard]] T* get(handle_type key) noexcept { return storage_.get(key); }
    [[nodiscard]] const T* get(handle_type key) const noexcept { return storage_.get(key); }
    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }
    [[nodiscard]] bool empty() const noexcept { return storage_.empty(); }
    template <class F> void for_each(F&& function) { storage_.for_each(std::forward<F>(function)); }
    template <class F> void for_each(F&& function) const { storage_.for_each(std::forward<F>(function)); }
    void clear() { storage_.clear(); }
private:
    SlotStorage<T, handle_type, 0> storage_;
};

} // namespace mlibs::memory
