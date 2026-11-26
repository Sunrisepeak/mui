module mlibs.delivery;

import std;
import mcpplibs.os;

namespace mlibs::delivery {

namespace {

std::string generic(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else out += c;
        }
    }
    return out + "\"";
}

std::uintmax_t measured(const File& file, Measure measure) { return measure == Measure::bytes ? file.bytes : measure == Measure::gzip ? file.gzip : file.sent(); }

// `pattern` from `p` against `path` from `s`.
bool match(std::string_view pattern, std::string_view path) noexcept {
    if (pattern.empty()) return path.empty();
    if (pattern.starts_with("**")) {
        // Any run, directories too; `**/` also matches nothing ("**/index.html" is "index.html" at the top).
        const std::string_view rest = pattern.substr(pattern.starts_with("**/") ? 3 : 2);
        for (std::size_t skip = 0; skip <= path.size(); ++skip)
            if ((skip == 0 || !pattern.starts_with("**/") || path[skip - 1] == '/') && match(rest, path.substr(skip))) return true;
        return false;
    }
    if (pattern.front() == '*') {
        for (std::size_t skip = 0; skip <= path.size(); ++skip) {
            if (match(pattern.substr(1), path.substr(skip))) return true;
            if (skip < path.size() && path[skip] == '/') break;
        }
        return false;
    }
    if (path.empty()) return false;
    if (pattern.front() == '?' ? path.front() != '/' : pattern.front() == path.front()) return match(pattern.substr(1), path.substr(1));
    return false;
}

}  // namespace

bool compressed_kind(std::string_view extension) noexcept {
    constexpr std::array<std::string_view, 12> kinds{".html", ".md", ".json", ".js", ".mjs", ".css", ".svg", ".xml", ".txt", ".wasm", ".ttf", ".map"};
    return std::ranges::contains(kinds, extension);
}

bool matches(std::string_view pattern, std::string_view path) noexcept { return match(pattern, path); }

File measured(std::string path, std::string_view bytes) {
    File file;
    file.bytes = bytes.size();
    file.gzip = gzip(bytes, 9).size();
    const std::size_t slash = path.rfind('/'), dot = path.rfind('.');
    std::string extension = dot != std::string::npos && (slash == std::string::npos || dot > slash) ? path.substr(dot) : std::string{};
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    file.compressed = compressed_kind(extension);
    file.path = std::move(path);
    return file;
}

bool Manifest::within() const noexcept { return std::ranges::all_of(verdicts, &Verdict::within); }

Manifest held(std::vector<File> files, std::span<const Budget> budgets) {
    Manifest manifest;
    std::ranges::sort(files, {}, &File::path);
    for (const File& file : files) manifest.bytes += file.bytes, manifest.gzip += file.gzip, manifest.sent += file.sent();
    for (const Budget& budget : budgets) {
        Verdict verdict{.budget = budget};
        std::uintmax_t sum = 0, largest = 0;
        for (const File& file : files) {
            if (!matches(budget.pattern, file.path)) continue;
            ++verdict.files;
            const std::uintmax_t weighs = measured(file, budget.measure);
            sum += weighs;
            if (weighs >= largest) largest = weighs, verdict.where = file.path;
        }
        verdict.found = budget.over == Over::each ? largest : sum;
        if (budget.over == Over::sum) verdict.where.clear();
        verdict.within = budget.most == 0 || verdict.found <= budget.most;  // none stated: a share, counted and held to nothing
        manifest.verdicts.push_back(std::move(verdict));
    }
    manifest.files = std::move(files);
    return manifest;
}

std::expected<Manifest, std::string> measure(const std::filesystem::path& directory, std::span<const Budget> budgets) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) return std::unexpected(std::format("{} is no directory: build first", mcpplibs::os::fs::utf8(directory)));
    std::vector<File> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
        if (!entry.is_regular_file()) continue;
        const auto bytes = mcpplibs::os::fs::read(entry.path());
        if (!bytes) return std::unexpected(std::format("{}: {}", mcpplibs::os::fs::utf8(entry.path()), bytes.error().message()));
        files.push_back(measured(generic(entry.path().lexically_relative(directory)), *bytes));
    }
    if (error) return std::unexpected(std::format("{}: {}", mcpplibs::os::fs::utf8(directory), error.message()));
    return held(std::move(files), budgets);
}

std::string to_json(const Manifest& manifest) {
    std::string out = std::format("{{\"$format\":\"delivery@1\",\"bytes\":{},\"gzip\":{},\"sent\":{},\"within\":{},\"budgets\":[", manifest.bytes, manifest.gzip, manifest.sent,
                                  manifest.within());
    for (std::size_t at = 0; at < manifest.verdicts.size(); ++at) {
        const Verdict& verdict = manifest.verdicts[at];
        out += std::format("{}{{\"name\":{},\"pattern\":{},\"measure\":\"{}\",\"over\":\"{}\",\"most\":{},\"found\":{},\"where\":{},\"files\":{},\"within\":{}}}", at ? "," : "",
                           json_text(verdict.budget.name), json_text(verdict.budget.pattern), name(verdict.budget.measure), name(verdict.budget.over), verdict.budget.most, verdict.found,
                           json_text(verdict.where), verdict.files, verdict.within);
    }
    out += "],\"files\":[";
    for (std::size_t at = 0; at < manifest.files.size(); ++at) {
        const File& file = manifest.files[at];
        out += std::format("{}{{\"path\":{},\"bytes\":{},\"gzip\":{},\"compressed\":{}}}", at ? "," : "", json_text(file.path), file.bytes, file.gzip, file.compressed);
    }
    return out + "]}\n";
}

}  // namespace mlibs::delivery
