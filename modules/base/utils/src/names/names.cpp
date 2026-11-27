module mlibs.names;

import std;

namespace mlibs::names {

std::size_t distance(std::string_view a, std::string_view b) {
    const std::size_t width = b.size() + 1;
    std::vector<std::size_t> cost((a.size() + 1) * width);
    for (std::size_t i = 0; i <= a.size(); ++i) cost[i * width] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) cost[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t v = std::min({cost[(i - 1) * width + j] + 1, cost[i * width + j - 1] + 1, cost[(i - 1) * width + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, cost[(i - 2) * width + j - 2] + 1);
            cost[i * width + j] = v;
        }
    return cost[a.size() * width + b.size()];
}

std::string nearest(std::string_view name, std::span<const std::string_view> options) {
    std::string_view best;
    // Within a third of the name, and never a whole one-letter name: `l` is not a typo of `s`.
    std::size_t within = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (std::string_view option : options)
        if (const std::size_t edits = distance(name, option); edits < within) within = edits, best = option;
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (std::string_view option : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", option);
    return std::format("one of {}", all);
}

}  // namespace mlibs::names
