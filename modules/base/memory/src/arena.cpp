module openmui.memory;

import std;

namespace omui {

namespace {

// `size` bytes at `align` in the block's free end, or null.
void* fit(std::byte* data, std::size_t capacity, std::size_t& used, std::size_t size, std::size_t align) noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(data);
    const std::uintptr_t at = (base + used + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
    const std::size_t end = static_cast<std::size_t>(at - base) + size;
    if (end > capacity) return nullptr;
    used = end;
    return reinterpret_cast<void*>(at);
}

}  // namespace

void* Arena::allocate(std::size_t size, std::size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) align = alignof(std::max_align_t);
    // Small requests fill the blocks in order; a full block is left behind.
    while (current_ < blocks_.size()) {
        Block& b = blocks_[current_];
        if (void* p = fit(b.data.get(), b.size, b.used, size, align)) return p;
        if (size > block_size_) break;  // large: does not give up the current block for the small ones after it
        ++current_;
    }
    // A large request (an upload's pixels) takes the block after it whose
    // room fits it best — one a frame of the same shape made last time — so
    // a frame of the same sizes allocates nothing new, in whatever order.
    if (size > block_size_) {
        Block* best = nullptr;
        for (std::size_t i = current_ + 1; i < blocks_.size(); ++i) {
            Block& b = blocks_[i];
            if (b.size - b.used >= size + align && (!best || b.size - b.used < best->size - best->used)) best = &b;
        }
        if (best)
            if (void* p = fit(best->data.get(), best->size, best->used, size, align)) return p;
    }
    std::size_t need = std::max(block_size_, size + align);
    Block b;
    b.data = std::make_unique_for_overwrite<std::byte[]>(need);
    b.size = need;
    ++heap_allocations_;
    if (size > block_size_ && current_ < blocks_.size()) {
        // After the blocks in use: the current block stays current.
        blocks_.push_back(std::move(b));
        Block& placed = blocks_.back();
        return fit(placed.data.get(), placed.size, placed.used, size, align);
    }
    blocks_.push_back(std::move(b));
    current_ = blocks_.size() - 1;
    return allocate(size, align);
}

std::string_view Arena::copy(std::string_view text) {
    if (text.empty()) return {};
    auto* p = static_cast<char*>(allocate(text.size(), 1));
    std::memcpy(p, text.data(), text.size());
    return {p, text.size()};
}

void Arena::reset() noexcept {
    for (Block& b : blocks_) b.used = 0;
    current_ = 0;
}

std::size_t Arena::used() const noexcept {
    std::size_t n = 0;
    for (const Block& b : blocks_) n += b.used;
    return n;
}

std::size_t Arena::capacity() const noexcept {
    std::size_t n = 0;
    for (const Block& b : blocks_) n += b.size;
    return n;
}

}  // namespace omui
