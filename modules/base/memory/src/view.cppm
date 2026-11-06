// mlibs.memory.api:view — a borrowed contiguous run that may name an
// incomplete element type.
//
// std::span<T> cannot always be a member while T is incomplete: with C++23's
// const iterators some standard libraries (MSVC's STL) check iterator
// concepts when the class is instantiated, and those need sizeof(T). A paint
// that mixes paints is such a case (a Paint holds the run of WeightedPaints,
// a WeightedPaint holds a Paint). View<T> only names T* in its declarations;
// the members that need T's size are instantiated where they are used.
export module mlibs.memory.api:view;

import std;

export namespace mlibs::memory {

template <class T>
class View {
public:
    constexpr View() noexcept = default;
    constexpr View(T* data, std::size_t size) noexcept : data_(data), size_(size) {}
    // From any contiguous, sized range whose elements a T* can point to
    // (a std::span, a std::vector, an array), as std::span converts.
    template <class R>
        requires std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
                 std::convertible_to<std::remove_reference_t<std::ranges::range_reference_t<R>> (*)[], T (*)[]>
    constexpr View(R&& range) noexcept  // NOLINT: implicit by design, like std::span
        : data_(std::ranges::data(range)), size_(static_cast<std::size_t>(std::ranges::size(range))) {}

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr T* begin() const noexcept { return data_; }
    [[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }
    [[nodiscard]] constexpr T& operator[](std::size_t i) const noexcept { return data_[i]; }

private:
    T*          data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace mlibs::memory
