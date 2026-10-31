// openmui.memory:handle — generational handles and the slot map behind them.
//
// Objects the runtime hands out (nodes, signals, subscriptions, images) are
// referred to by a handle: an index and a generation. A handle to an object
// that has gone is detected (its generation no longer matches) instead of
// reading freed memory, which is what lets a script or an agent hold a handle
// across frames safely.
export module openmui.memory:handle;

import std;

export namespace omui {

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

// Dense storage with stable handles. Erased slots are reused; their
// generation is bumped so old handles stop matching.
template <class T, class Tag>
class SlotMap {
public:
    using handle_type = Handle<Tag>;

    handle_type insert(T value) {
        std::uint32_t i;
        if (!free_.empty()) {
            i = free_.back();
            free_.pop_back();
            slots_[i].value.emplace(std::move(value));
        } else {
            i = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({std::optional<T>(std::move(value)), 0});
        }
        ++size_;
        return {i, slots_[i].generation};
    }

    bool erase(handle_type h) {
        if (!contains(h)) return false;
        slots_[h.index].value.reset();
        ++slots_[h.index].generation;
        free_.push_back(h.index);
        --size_;
        return true;
    }

    [[nodiscard]] bool contains(handle_type h) const noexcept {
        return h.index < slots_.size() && slots_[h.index].generation == h.generation && slots_[h.index].value.has_value();
    }

    [[nodiscard]] T* get(handle_type h) noexcept { return contains(h) ? &*slots_[h.index].value : nullptr; }
    [[nodiscard]] const T* get(handle_type h) const noexcept { return contains(h) ? &*slots_[h.index].value : nullptr; }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    template <class F>
    void for_each(F&& f) {
        for (std::uint32_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) f(handle_type{i, slots_[i].generation}, *slots_[i].value);
    }

    void clear() {
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].value) continue;
            slots_[i].value.reset();
            ++slots_[i].generation;
            free_.push_back(i);
        }
        size_ = 0;
    }

private:
    struct Slot {
        std::optional<T> value;
        std::uint32_t    generation = 0;
    };
    std::vector<Slot>          slots_;
    std::vector<std::uint32_t> free_;
    std::size_t                size_ = 0;
};

}  // namespace omui
