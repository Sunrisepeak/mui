// openmui.hash — FNV-1a, once for the whole repository.
//
//     omui::hash::Fnv1a h;
//     h.text("frame").u64(17).f32(0.5f);
//     std::println("{}", h.hex());                    // what a test pins
//     const auto key = omui::hash::fnv1a(path);         // a stable key from a name
//
// FNV-1a over bytes (offset basis 0xcbf29ce484222325, prime 0x100000001b3) is
// what every pinned hash in openmui is: the same value on every platform and
// compiler. Numbers go in as their little-endian bytes, so a hash does not
// depend on the machine's byte order. Two other ways are kept apart by name:
// word() folds a whole value in with one multiply (quicker, for in-memory
// keys: not FNV-1a of its bytes), object() hashes an object's own bytes (its
// padding too: for types without any).
export module openmui.hash;

import std;

export namespace omui::hash {

class Fnv1a {
public:
    static constexpr std::uint64_t offset = 0xcbf29ce484222325ull;
    static constexpr std::uint64_t prime = 0x100000001b3ull;

    constexpr Fnv1a() noexcept = default;
    // Going on from a value (another hash, a seed).
    constexpr explicit Fnv1a(std::uint64_t start) noexcept : h_(start) {}

    constexpr Fnv1a& byte(std::uint8_t b) noexcept {
        h_ = (h_ ^ b) * prime;
        return *this;
    }
    constexpr Fnv1a& bytes(std::span<const std::uint8_t> b) noexcept {
        for (const std::uint8_t x : b) byte(x);
        return *this;
    }
    Fnv1a& bytes(std::span<const std::byte> b) noexcept {
        for (const std::byte x : b) byte(static_cast<std::uint8_t>(x));
        return *this;
    }
    // A string's bytes (no length, no end: add byte() or str() to keep "ab", "c" apart from "a", "bc").
    constexpr Fnv1a& text(std::string_view s) noexcept {
        for (const char c : s) byte(static_cast<std::uint8_t>(c));
        return *this;
    }
    // A string's bytes and its length: one string of a sequence of them.
    constexpr Fnv1a& str(std::string_view s) noexcept { return text(s).u64(s.size()); }
    // A value's low `count` bytes, least significant first.
    constexpr Fnv1a& le(std::uint64_t v, int count = 8) noexcept {
        for (int i = 0; i < count; ++i) byte(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
        return *this;
    }
    constexpr Fnv1a& u64(std::uint64_t v) noexcept { return le(v, 8); }
    constexpr Fnv1a& i64(std::int64_t v) noexcept { return le(static_cast<std::uint64_t>(v), 8); }
    constexpr Fnv1a& f64(double v) noexcept { return u64(std::bit_cast<std::uint64_t>(v)); }
    // A float's bits, widened to eight bytes.
    constexpr Fnv1a& f32(float v) noexcept { return u64(std::bit_cast<std::uint32_t>(v)); }
    // A whole value folded in at once, one multiply: quicker, and not the
    // FNV-1a of its bytes (a key kept in memory, a running checksum).
    constexpr Fnv1a& word(std::uint64_t v) noexcept {
        h_ = (h_ ^ v) * prime;
        return *this;
    }
    // An object's own bytes, padding and all: for types that have none.
    template <class T>
        requires std::is_trivially_copyable_v<T>
    Fnv1a& object(const T& v) noexcept {
        return bytes(std::as_bytes(std::span(&v, 1)));
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return h_; }
    // Sixteen hex digits, as tests print and pin it.
    [[nodiscard]] std::string hex() const;

private:
    std::uint64_t h_ = offset;
};

// FNV-1a 64 of bytes or a string.
[[nodiscard]] constexpr std::uint64_t fnv1a(std::string_view s) noexcept { return Fnv1a().text(s).value(); }
[[nodiscard]] inline std::uint64_t fnv1a(std::span<const std::byte> b) noexcept { return Fnv1a().bytes(b).value(); }

// FNV-1a 32 (offset basis 2166136261, prime 16777619): short generated ids.
class Fnv1a32 {
public:
    static constexpr std::uint32_t offset = 2166136261u;
    static constexpr std::uint32_t prime = 16777619u;
    constexpr Fnv1a32& byte(std::uint8_t b) noexcept {
        h_ = (h_ ^ b) * prime;
        return *this;
    }
    constexpr Fnv1a32& text(std::string_view s) noexcept {
        for (const char c : s) byte(static_cast<std::uint8_t>(c));
        return *this;
    }
    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return h_; }

private:
    std::uint32_t h_ = offset;
};
[[nodiscard]] constexpr std::uint32_t fnv1a32(std::string_view s) noexcept { return Fnv1a32().text(s).value(); }

// The reference vectors (FNV's own test suite): checked at compile time.
static_assert(fnv1a("") == 0xcbf29ce484222325ull && fnv1a("a") == 0xaf63dc4c8601ec8cull && fnv1a("foobar") == 0x85944171f73967e8ull);
static_assert(fnv1a32("") == 2166136261u && fnv1a32("a") == 0xe40c292cu && fnv1a32("foobar") == 0xbf9cf968u);

}  // namespace omui::hash
