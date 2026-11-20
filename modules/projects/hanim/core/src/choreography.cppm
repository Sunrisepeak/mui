// hanim.core:choreography — many elements on one course (a text's letters, a
// list's rows, a burst's sparks): in which order they start, and a course
// repeated. The arithmetic the UI's glyph motion and any other staggered
// effect share (single ownership: hanim owns animation).
export module hanim.core:choreography;

import std;
import :math;

export namespace hanim {

enum class StaggerOrder : std::uint8_t { forward, reverse, center, edges, random };

// Each element's start slot, by its rank in reading order (the elements'
// stable keys, sorted ascending): forward and reverse by rank, center from
// the middle out, edges from both ends in, random by `seed` and each key.
[[nodiscard]] inline std::vector<std::size_t>
stagger_slots(std::span<const std::uint64_t> keys, StaggerOrder order, std::uint64_t seed) {
    const std::size_t count = keys.size();
    std::vector<std::size_t> slot(count);
    const auto middle = [&](std::size_t rank) {
        return static_cast<std::size_t>(
            std::abs(2 * static_cast<long long>(rank) - static_cast<long long>(count - 1)) / 2);
    };
    for (std::size_t rank = 0; rank < count; ++rank) {
        switch (order) {
            case StaggerOrder::forward:
                slot[rank] = rank;
                break;
            case StaggerOrder::reverse:
                slot[rank] = count - 1 - rank;
                break;
            case StaggerOrder::center:
                slot[rank] = middle(rank);
                break;
            case StaggerOrder::edges:
                slot[rank] = (count - 1) / 2 - middle(rank);
                break;
            case StaggerOrder::random:
                break;
        }
    }
    if (order == StaggerOrder::random) {
        std::vector<std::pair<std::uint64_t, std::size_t>> shuffled(count);
        for (std::size_t rank = 0; rank < count; ++rank)
            shuffled[rank] = {mlibs::math::mix64(seed ^ mlibs::math::mix64(keys[rank])), rank};
        std::ranges::sort(shuffled);
        for (std::size_t place = 0; place < count; ++place)
            slot[shuffled[place].second] = place;
    }
    return slot;
}

// A course repeated `repeat` times (0: forever), each `cycle` seconds long:
// the progress `elapsed` seconds in, `course(local)` within a cycle, every
// other cycle's value flipped (1 − p) when alternating. Before 0 it is 0;
// after the last cycle it holds the last cycle's end.
template <class Course>
[[nodiscard]] double repeated(double elapsed, double cycle, std::uint32_t repeat, bool alternate,
                              Course&& course) {
    if (elapsed <= 0)
        return 0;
    const double count = std::floor(elapsed / cycle);
    if (repeat > 0 && count >= repeat)
        return alternate && repeat % 2 == 0 ? 0.0 : 1.0;
    double progress = course(elapsed - count * cycle);
    if (alternate && static_cast<long long>(count) % 2 == 1)
        progress = 1 - progress;
    return progress;
}

}  // namespace hanim
