// openmui.memory:arena — the per-frame allocator.
//
// What a frame builds and throws away (display-list payloads, layout scratch,
// event lists) is bump-allocated here and released all at once by `reset`.
// Blocks are kept across resets, so a frame of the same shape as the last
// allocates nothing from the heap: the zero-allocation rule of plan §1.4.
export module openmui.memory:arena;

import std;

export namespace omui {

class Arena {
public:
    explicit Arena(std::size_t block_size = 64 * 1024) : block_size_(block_size) {}
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) noexcept = default;
    Arena& operator=(Arena&&) noexcept = default;

    [[nodiscard]] void* allocate(std::size_t size, std::size_t align);

    // Objects in an arena are never destroyed one by one, so only trivially
    // destructible types may live there.
    template <class T, class... Args>
        requires std::is_trivially_destructible_v<T>
    [[nodiscard]] T* make(Args&&... args) {
        return std::construct_at(static_cast<T*>(allocate(sizeof(T), alignof(T))), std::forward<Args>(args)...);
    }

    template <class T>
        requires std::is_trivially_destructible_v<T> && std::is_trivially_default_constructible_v<T>
    [[nodiscard]] std::span<T> array(std::size_t n) {
        if (n == 0) return {};
        T* p = static_cast<T*>(allocate(sizeof(T) * n, alignof(T)));
        for (std::size_t i = 0; i < n; ++i) std::construct_at(p + i);
        return {p, n};
    }

    template <class T>
        requires std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>
    [[nodiscard]] std::span<T> copy(std::span<const T> from) {
        if (from.empty()) return {};
        T* p = static_cast<T*>(allocate(sizeof(T) * from.size(), alignof(T)));
        std::memcpy(p, from.data(), sizeof(T) * from.size());
        return {p, from.size()};
    }

    [[nodiscard]] std::string_view copy(std::string_view text);

    // Releases everything; keeps the blocks.
    void reset() noexcept;

    [[nodiscard]] std::size_t used() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    // Heap allocations made since construction (for zero-allocation tests).
    [[nodiscard]] std::size_t heap_allocations() const noexcept { return heap_allocations_; }

private:
    struct Block {
        std::unique_ptr<std::byte[]> data;
        std::size_t                  size = 0;
        std::size_t                  used = 0;
    };
    std::vector<Block> blocks_;
    std::size_t        current_ = 0;
    std::size_t        block_size_;
    std::size_t        heap_allocations_ = 0;
};

}  // namespace omui
