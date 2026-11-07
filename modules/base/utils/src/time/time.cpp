module mlibs.time;

import std;

namespace mlibs::time {

Flicks from_seconds(double s) noexcept { return static_cast<Flicks>(std::llround(s * static_cast<double>(flicks_per_second))); }

std::int64_t Rate::frame_at(seconds t) const noexcept {
    // t·num/den, then the floor. A frame's own start (computed by time_of, one
    // rounding) must map back to that frame after two more roundings here, so
    // the product is nudged up by a few ulps of itself — far less than the
    // distance to any earlier instant a caller can mean.
    double f = t.count() * static_cast<double>(num) / static_cast<double>(den);
    double n = std::floor(f + 4e-15 * (1.0 + (f < 0 ? -f : f)));
    return static_cast<std::int64_t>(n);
}

}  // namespace mlibs::time
