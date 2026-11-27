module mlibs.version;

import std;

namespace mlibs::version {

namespace {

std::optional<std::uint64_t> number(std::string_view part) {
    std::uint64_t value = 0;
    const auto [end, failure] = std::from_chars(part.data(), part.data() + part.size(), value);
    if (failure != std::errc{} || end != part.data() + part.size()) return std::nullopt;
    return value;
}

std::vector<std::string_view> parts(std::string_view text) {
    std::vector<std::string_view> out;
    for (const auto part : std::views::split(text, '.')) out.emplace_back(part.begin(), part.end());
    return out;
}

std::strong_ordering compare_parts(std::string_view first, std::string_view second) {
    const auto first_parts = parts(first), second_parts = parts(second);
    for (std::size_t index = 0; index < std::max(first_parts.size(), second_parts.size()); ++index) {
        const std::string_view mine = index < first_parts.size() ? first_parts[index] : "0";
        const std::string_view theirs = index < second_parts.size() ? second_parts[index] : "0";
        const auto mine_number = number(mine), their_number = number(theirs);
        const std::strong_ordering order = mine_number && their_number ? *mine_number <=> *their_number : mine.compare(theirs) <=> 0;
        if (order != 0) return order;
    }
    return std::strong_ordering::equal;
}

std::pair<std::string_view, std::string_view> core_and_pre(std::string_view version) {
    const auto dash = version.find('-');
    return {version.substr(0, dash), dash == std::string_view::npos ? std::string_view{} : version.substr(dash + 1)};
}

}  // namespace

std::strong_ordering compare(std::string_view left, std::string_view right) {
    const auto [left_core, left_pre] = core_and_pre(left);
    const auto [right_core, right_pre] = core_and_pre(right);
    if (const auto order = compare_parts(left_core, right_core); order != 0) return order;
    if (left_pre.empty() || right_pre.empty()) return left_pre.empty() <=> right_pre.empty();  // a release after its pre-releases
    return compare_parts(left_pre, right_pre);
}

}  // namespace mlibs::version
