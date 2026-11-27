module mlibs.apart;

import std;

namespace mlibs::apart {

namespace {

constexpr bool kind_character(char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
           character == '.' || character == '-' || character == '_';
}

}  // namespace

std::string mark(std::string_view kind, std::size_t length) {
    std::string out(prefix);
    out += kind;
    out += '\0';
    out += std::to_string(length);
    out += '\0';
    return out;
}

std::vector<Region> regions(std::span<const std::uint8_t> data) {
    const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
    std::vector<Region> found;
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t begins = text.find(prefix, from);
        if (begins == std::string_view::npos) break;
        from = begins + 1;
        std::size_t at = begins + prefix.size();
        const std::size_t kind_from = at;
        while (at < text.size() && kind_character(text[at])) ++at;
        if (at == kind_from || at >= text.size() || text[at] != '\0') continue;
        const std::string_view kind = text.substr(kind_from, at - kind_from);
        const std::size_t digits_from = ++at;
        std::size_t length = 0;
        const auto [end, error] = std::from_chars(text.data() + digits_from, text.data() + text.size(), length);
        at = static_cast<std::size_t>(end - text.data());
        if (error != std::errc{} || at == digits_from || at >= text.size() || text[at] != '\0') continue;
        ++at;
        if (length > text.size() - at) continue;
        found.push_back({std::string(kind), begins, at, length});
        from = at + length;
    }
    return found;
}

namespace {
std::atomic<Bring> installed{nullptr};
}  // namespace

void bring_with(Bring bring) noexcept { installed.store(bring); }

std::string_view present(std::string_view region) noexcept {
    // Read here, at run time, from where the region is: a reader's constant says what was linked, not what is there.
    if (region.empty() || region.front() != '\0') return region;
    const Bring bring = installed.load();
    if (bring && bring(data_kind) && region.front() != '\0') return region;
    return {};
}

}  // namespace mlibs::apart
