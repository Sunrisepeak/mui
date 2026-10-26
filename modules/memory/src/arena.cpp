module openmui.memory;

import std;

namespace omui {

void* Arena::allocate(std::size_t size, std::size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) align = alignof(std::max_align_t);
    while (current_ < blocks_.size()) {
        Block& b = blocks_[current_];
        auto base = reinterpret_cast<std::uintptr_t>(b.data.get());
        std::uintptr_t at = (base + b.used + align - 1) & ~(static_cast<std::uintptr_t>(align) - 1);
        std::size_t end = static_cast<std::size_t>(at - base) + size;
        if (end <= b.size) {
            b.used = end;
            return reinterpret_cast<void*>(at);
        }
        ++current_;
    }
    std::size_t need = std::max(block_size_, size + align);
    Block b;
    b.data = std::make_unique_for_overwrite<std::byte[]>(need);
    b.size = need;
    ++heap_allocations_;
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
