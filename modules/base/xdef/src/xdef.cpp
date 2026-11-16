// mlibs.xdef: names and format identifiers, and a reader's check of a snapshot's.
module mlibs.xdef;

import std;

namespace mlibs::xdef {

namespace {

bool is_word(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

}  // namespace

std::optional<FileName> parse_name(std::string_view file_name) {
    std::vector<std::string_view> parts;
    for (const auto part : std::views::split(file_name, '.')) parts.emplace_back(part.begin(), part.end());
    if (parts.size() < 4) return std::nullopt;
    const auto extension = parts[parts.size() - 1];
    const auto suffix = parts[parts.size() - 2];
    const auto kind = parts[parts.size() - 3];
    if (extension != "ts" && extension != "json" && extension != "jsonl") return std::nullopt;
    if (suffix != container && suffix != earlier_container) return std::nullopt;
    if (!is_word(kind)) return std::nullopt;
    const auto name = file_name.substr(0, file_name.size() - kind.size() - suffix.size() - extension.size() - 3);
    if (name.empty()) return std::nullopt;
    return FileName{std::string(name), std::string(kind), std::string(suffix), std::string(extension)};
}

std::optional<Format> parse_format(std::string_view text) {
    const auto at = text.rfind('@');
    if (at == std::string_view::npos || !is_word(text.substr(0, at))) return std::nullopt;
    int major = 0;
    const auto digits = text.substr(at + 1);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), major);
    if (error != std::errc{} || end != digits.data() + digits.size() || major < 1) return std::nullopt;
    return Format{std::string(text.substr(0, at)), major};
}

std::string format_id(const Format& format) { return std::format("{}@{}", format.kind, format.major); }

std::optional<std::string> format_problem(std::optional<std::string_view> found, std::string_view kind, int major) {
    const auto want = format_id({std::string(kind), major});
    if (!found) return std::format("no \"{}\" (\"{}\"): a snapshot names its format (D-209; `xdef snapshot` writes it)", format_key, want);
    const auto format = parse_format(*found);
    if (!format) return std::format("\"{}\" is no format identifier (<kind>@<major>): \"{}\"", format_key, *found);
    if (format->kind != kind) return std::format("a {} snapshot, not a {} one (\"{}\": \"{}\")", format->kind, kind, format_key, *found);
    if (format->major != major)
        return std::format("{} major version {}; this reader reads {} (its owner's migration takes it there)", kind, format->major, major);
    return std::nullopt;
}

}  // namespace mlibs::xdef
