// One generational slot/free-list mechanism for identity-owning libraries.
export module mlibs.memory.api:slots;

import std;

export namespace mlibs::memory {

// Wide keys have no packed ABI. Handle<Tag> retains its existing 32/32 form.
// valid() checks the index sentinel; only the owning storage proves liveness.
template <class Tag, class Generation = std::uint64_t>
struct SlotKey {
    static_assert(std::unsigned_integral<Generation> && !std::same_as<Generation, bool>);
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = invalid_index;
    Generation generation = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return index != invalid_index; }
    constexpr explicit operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(SlotKey, SlotKey) noexcept = default;
};

template <class H>
using slot_generation_t = std::remove_cvref_t<decltype(H{}.generation)>;

enum class SlotStorageError : std::uint8_t { capacity };

// Single owning thread or external synchronization. Queries never allocate.
// Slot count includes reusable vacancies and permanently retired slots, and
// is bounded by max_slots; construction does not preallocate that bound.
// Generation exhaustion retires a slot, never wraps it back to an old key.
template <class T, class H = SlotKey<T>, slot_generation_t<H> InitialGeneration = 1>
class SlotStorage {
public:
    using value_type = T;
    using handle_type = H;
    using generation_type = slot_generation_t<H>;
    static_assert(std::same_as<decltype(H{}.index), std::uint32_t>);
    static_assert(std::unsigned_integral<generation_type> && !std::same_as<generation_type, bool>);
    static_assert(H::invalid_index == std::numeric_limits<std::uint32_t>::max());

    explicit SlotStorage(std::size_t max_slots = H::invalid_index) noexcept
        : limit_(std::min(max_slots, static_cast<std::size_t>(H::invalid_index))) {}
    SlotStorage(const SlotStorage&) requires std::copy_constructible<T> = default;
    SlotStorage& operator=(const SlotStorage& other) requires std::copy_constructible<T> {
        if (this != &other) { SlotStorage staging(other); swap(staging); }
        return *this;
    }
    SlotStorage(SlotStorage&& other) noexcept
        : slots_(std::move(other.slots_)), head_(std::exchange(other.head_, H::invalid_index)),
          live_(std::exchange(other.live_, 0)), retired_(std::exchange(other.retired_, 0)), limit_(other.limit_) {
        other.slots_.clear();
    }
    SlotStorage& operator=(SlotStorage&& other) noexcept {
        if (this != &other) {
            slots_ = std::move(other.slots_);
            head_ = std::exchange(other.head_, H::invalid_index);
            live_ = std::exchange(other.live_, 0);
            retired_ = std::exchange(other.retired_, 0);
            limit_ = other.limit_;
            other.slots_.clear();
        }
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept { return live_; }
    [[nodiscard]] bool empty() const noexcept { return live_ == 0; }
    [[nodiscard]] std::size_t slot_count() const noexcept { return slots_.size(); }
    [[nodiscard]] std::size_t retired_count() const noexcept { return retired_; }
    [[nodiscard]] std::size_t max_slots() const noexcept { return limit_; }

    // A preview, not a reservation; it is valid only until the next mutation.
    [[nodiscard]] std::optional<H> next_handle() const noexcept {
        if (head_ != H::invalid_index) return H{head_, slots_[head_].generation};
        if (slots_.size() >= limit_) return std::nullopt;
        return H{static_cast<std::uint32_t>(slots_.size()), InitialGeneration};
    }

    // make(candidate) must return exactly expected<T,E>. Full storage does
    // not invoke it. A returned error is passed through without changing any
    // slot, generation, live count, free-list or vector capacity. Commit only
    // begins after successful preparation. Allocation failure follows the
    // program's allocation policy, rather than becoming a factory error.
    // PRECONDITION: make may inspect this storage, but must not mutate,
    // reenter a mutation, swap, move or destroy it. Side effects outside this
    // storage belong to the caller. No query-path guard/state is introduced.
    template <class Factory>
    auto try_insert(Factory&& make, typename std::invoke_result_t<Factory&, H>::error_type capacity_error)
        -> std::expected<H, typename std::invoke_result_t<Factory&, H>::error_type> {
        using Result = std::invoke_result_t<Factory&, H>;
        using Error = typename Result::error_type;
        static_assert(std::same_as<Result, std::expected<T, Error>>, "slot factory must return expected<T,E> by value");
        const auto candidate = next_handle();
        if (!candidate) return std::unexpected(std::move(capacity_error));
        auto prepared = std::invoke(make, H{*candidate});
        if (!prepared) return std::unexpected(std::move(prepared.error()));
        commit(*candidate, std::move(*prepared));
        return *candidate;
    }

    template <class U>
    [[nodiscard]] std::expected<H, SlotStorageError> insert(U&& value) requires std::constructible_from<T, U&&> {
        const auto candidate = next_handle();
        if (!candidate) return std::unexpected(SlotStorageError::capacity);
        commit(*candidate, std::forward<U>(value));
        return *candidate;
    }
    [[nodiscard]] bool contains(H key) const noexcept {
        return key.index < slots_.size() && slots_[key.index].generation == key.generation && slots_[key.index].value.has_value();
    }
    [[nodiscard]] T* get(H key) noexcept { return contains(key) ? &*slots_[key.index].value : nullptr; }
    [[nodiscard]] const T* get(H key) const noexcept { return contains(key) ? &*slots_[key.index].value : nullptr; }
    bool erase(H key) {
        if (!contains(key)) return false;
        release(key.index);
        return true;
    }
    // Keep slot history so clear cannot make any previously live key valid
    // again. Existing free-list entries and retired slots remain intact.
    void clear() {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) release(static_cast<std::uint32_t>(i));
    }
    template <class F>
    void for_each(F&& function) {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) std::invoke(function, H{static_cast<std::uint32_t>(i), slots_[i].generation}, *slots_[i].value);
    }
    template <class F>
    void for_each(F&& function) const {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].value) std::invoke(function, H{static_cast<std::uint32_t>(i), slots_[i].generation}, *slots_[i].value);
    }
    void swap(SlotStorage& other) noexcept {
        slots_.swap(other.slots_);
        std::swap(head_, other.head_); std::swap(live_, other.live_);
        std::swap(retired_, other.retired_); std::swap(limit_, other.limit_);
    }
    friend void swap(SlotStorage& a, SlotStorage& b) noexcept { a.swap(b); }

private:
    struct Slot {
        template <class U>
        Slot(std::in_place_t, U&& initial) : value(std::in_place, std::forward<U>(initial)) {}
        std::optional<T> value;
        generation_type generation = InitialGeneration;
        std::uint32_t next_free = H::invalid_index;
    };
    template <class U>
    void commit(H key, U&& value) {
        if (key.index == slots_.size()) slots_.emplace_back(std::in_place, std::forward<U>(value));
        else {
            Slot& slot = slots_[key.index];
            slot.value.emplace(std::forward<U>(value));
            head_ = slot.next_free;
            slot.next_free = H::invalid_index;
        }
        ++live_;
    }
    void release(std::uint32_t index) {
        Slot& slot = slots_[index];
        slot.value.reset(); --live_;
        if (slot.generation == std::numeric_limits<generation_type>::max()) {
            slot.next_free = H::invalid_index; ++retired_;
        } else {
            ++slot.generation;
            slot.next_free = head_; head_ = index;
        }
    }
    std::vector<Slot> slots_;
    std::uint32_t head_ = H::invalid_index;
    std::size_t live_ = 0, retired_ = 0, limit_;
};

} // namespace mlibs::memory
