module mlibs.message;

import std;

namespace mlibs::message {

std::string fill(std::string_view pattern, std::span<const std::string_view> values) {
    std::size_t size = pattern.size();
    for (const std::string_view value : values) size += value.size();
    std::string out;
    out.reserve(size);
    std::size_t next = 0;
    for (std::size_t at = 0; at < pattern.size(); ++at) {
        const char here = pattern[at];
        const char after = at + 1 < pattern.size() ? pattern[at + 1] : '\0';
        if ((here == '{' && after == '{') || (here == '}' && after == '}')) {
            out += here;
            ++at;
        } else if (here == '{' && after == '}') {
            if (next < values.size()) out += values[next++];
            ++at;
        } else {
            out += here;
        }
    }
    return out;
}

namespace {

template <class Number>
std::string written(Number value) {
    std::array<char, 32> buffer{};
    const auto [end, failed] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return failed == std::errc{} ? std::string(buffer.data(), end) : std::string("?");
}

}  // namespace

std::string text(std::int64_t value) { return written(value); }
std::string text(std::uint64_t value) { return written(value); }
std::string text(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    return written(value);
}

}  // namespace mlibs::message
