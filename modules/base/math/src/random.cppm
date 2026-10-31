// openmui.math:random — the randomness that must be the same everywhere (a
// spark's scatter after a rollback, a test's data, a glyph's jitter):
// splitmix64 (Steele, Lea and Flood, 2014; Vigna's reference code), integer
// arithmetic only.
//
//     SplitMix64 rng{seed};                 // a stream
//     const auto bits = rng.next();
//     const double u = unit24(mix64(key));  // a number from a key, no state
export module openmui.math:random;

import std;

export namespace omui::math {

// The golden ratio's 64-bit fraction: splitmix64's increment, and Fibonacci hashing's multiplier.
inline constexpr std::uint64_t golden = 0x9e3779b97f4a7c15ull;

// splitmix64's finaliser: every bit of the input moves every bit of the result.
[[nodiscard]] constexpr std::uint64_t finalize(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// splitmix64's output for state x: a number from a key, without a stream.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept { return finalize(x + golden); }

// A stream: next() is mix64 of the state before, the state advanced by golden.
struct SplitMix64 {
    std::uint64_t state = 0;
    constexpr std::uint64_t next() noexcept {
        state += golden;
        return finalize(state);
    }
};

// [0, 1) from a number's top 24 bits (a float's precision) or 53 (a double's).
[[nodiscard]] constexpr double unit24(std::uint64_t bits) noexcept { return static_cast<double>(bits >> 40) / static_cast<double>(1u << 24); }
[[nodiscard]] constexpr double unit53(std::uint64_t bits) noexcept { return static_cast<double>(bits >> 11) * (1.0 / 9007199254740992.0); }

// Vigna's reference: the first outputs of a stream from 1234567.
static_assert(SplitMix64{1234567}.next() == 6457827717110365317ull);

}  // namespace omui::math
