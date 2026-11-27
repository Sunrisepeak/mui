// mlibs.cts:compare — what a case compares, each answering what differed
// where, so a failure says it.
//
//   exact      bytes equal (level A): the first difference
//   within     bytes within ±tolerance, at most `budget` beyond it (level B)
//   image      two RGBA8 pictures: the largest difference, how many pixels
//              exceed the tolerance, the first one (x, y)
//   pinned     an FNV-1a hash of bytes against a pinned hex (mlibs.hash)
//   sampled    a process sampled at given times (the hash of each frame)
//              against pinned hashes: endpoints, intermediate states,
//              interruptions — never two ends alone
export module mlibs.cts:compare;

import std;
import :core;

export namespace mlibs::cts {

struct Difference {
    bool                       same = true;
    std::size_t                first = 0;       // the first differing (or exceeding) byte
    int                        largest = 0;     // the largest absolute difference
    std::size_t                beyond = 0;      // bytes beyond the tolerance
    std::string                text() const;
};

[[nodiscard]] Difference exact(std::span<const std::byte> got, std::span<const std::byte> want) noexcept;
[[nodiscard]] Difference within(std::span<const std::byte> got, std::span<const std::byte> want, int tolerance, std::size_t budget) noexcept;

// A picture: 8-bit RGBA rows of `stride` bytes.
struct Picture {
    std::span<const std::byte> bytes;
    int                        width = 0, height = 0;
    std::size_t                stride = 0;
};

struct PictureDifference {
    bool        same = true;
    int         largest = 0;
    std::size_t beyond = 0;       // pixels with a channel beyond the tolerance
    int         x = -1, y = -1;   // the first such pixel
    std::string text() const;
};

[[nodiscard]] PictureDifference image(const Picture& got, const Picture& want, int tolerance = 0, std::size_t budget = 0);

// The FNV-1a hash of bytes, as tests pin it (16 hex digits).
[[nodiscard]] std::string hash(std::span<const std::byte> bytes);

// Checks bytes against a pinned hash and records the hash as evidence.
bool pinned(Check& c, std::span<const std::byte> bytes, std::string_view hex, std::string_view what,
            std::source_location at = std::source_location::current());

// A process sampled: at each time, the hash of what `frame(t)` gives; checked
// against `pins` (one per time) and recorded as evidence.
struct Sample {
    double      seconds = 0;
    std::string hex;
};
bool sampled(Check& c, std::span<const double> times, const std::function<std::vector<std::byte>(double)>& frame,
             std::span<const std::string_view> pins, std::string_view what, std::source_location at = std::source_location::current());

}  // namespace mlibs::cts
