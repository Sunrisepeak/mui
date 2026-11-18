module omui.devtools.rename;

import std;
import mcpplibs.os;
import omui.devtools.run;

namespace omui::devtools::rename {

namespace {

// A code name's word: letters and digits on neither side. '_' ends one, so what is made from the name follows it
// (`xtools_test.cpp`, `run_xtools`); `xtoolsy` and `myxtools` are other words.
bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

bool code_name(std::string_view name) {
    return !name.empty() && std::islower(static_cast<unsigned char>(name.front())) &&
           std::ranges::all_of(name, [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)); });
}

// Whose text is left as it was: the decisions and the record (`.agents/`'s pages), under the names of their time —
// the data kept there (the evidence ledger) follows; what is not text (moved, its bytes untouched).
bool kept(std::string_view path) {
    if (path.starts_with(".agents/") && path.ends_with(".md")) return true;
    constexpr std::array binary{".png", ".webp", ".jpg", ".glb", ".gltf", ".hdr", ".wasm", ".woff2", ".ttf", ".otf", ".wav", ".ogg", ".opus", ".webm", ".ktx2", ".bin", ".zip"};
    return std::ranges::any_of(binary, [&](std::string_view suffix) { return path.ends_with(suffix); });
}

std::filesystem::path mcpp_path(std::string_view path) { return mcpplibs::os::fs::from_utf8(path); }

std::expected<std::vector<std::string>, std::string> tracked(const std::filesystem::path& root) {
    auto listed = run::output(root, "git", {"ls-files", "-z"});
    if (!listed) return std::unexpected(listed.error());
    std::vector<std::string> paths;
    for (const auto part : std::views::split(*listed, '\0'))
        if (!part.empty()) paths.emplace_back(part.begin(), part.end());
    return paths;
}

}  // namespace

std::string renamed(std::string_view text, std::string_view from, std::string_view to, std::size_t* count, std::span<const std::string> kept_names) {
    // Where a kept name stands (a record page's file name): the word inside it stays.
    std::vector<std::pair<std::size_t, std::size_t>> kept_spans;
    for (const auto& name : kept_names)
        for (std::size_t at = text.find(name); !name.empty() && at != std::string_view::npos; at = text.find(name, at + 1)) kept_spans.emplace_back(at, at + name.size());
    const auto inside_kept = [&](std::size_t where) {
        return std::ranges::any_of(kept_spans, [&](const auto& span) { return span.first <= where && where < span.second; });
    };
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0, found = 0;
    while (true) {
        const std::size_t next = text.find(from, at);
        if (next == std::string_view::npos) break;
        const bool before = next > 0 && word_char(text[next - 1]);
        const bool after = next + from.size() < text.size() && word_char(text[next + from.size()]);
        out.append(text.substr(at, next - at));
        if (before || after || inside_kept(next)) out.append(from);
        else {
            out.append(to);
            ++found;
        }
        at = next + from.size();
    }
    out.append(text.substr(at));
    if (count) *count = found;
    return out;
}

std::expected<Plan, std::string> plan(const std::filesystem::path& root, std::string_view from, std::string_view to) {
    if (!code_name(from) || !code_name(to)) return std::unexpected("a code name is lowercase letters and digits, a letter first (D-208: xrender, xtools)");
    if (from == to) return std::unexpected("the names are the same");
    auto paths = tracked(root);
    if (!paths) return std::unexpected(paths.error());
    Plan out;
    for (const std::string& path : *paths)
        if (path.starts_with(".agents/") && path.ends_with(".md")) {
            const std::string name = mcpplibs::os::fs::utf8(mcpplibs::os::fs::from_utf8(path).filename());
            if (renamed(name, from, to) != name) out.kept_names.push_back(name);
        }
    for (const std::string& path : *paths) {
        // A path moves whatever its content (a picture under a renamed directory too); the record's pages keep theirs.
        if (!(path.starts_with(".agents/") && path.ends_with(".md")))
            if (const std::string moved = renamed(path, from, to); moved != path) out.moves.emplace_back(path, moved);
        if (kept(path)) continue;
        const auto text = mcpplibs::os::fs::read(root / mcpplibs::os::fs::from_utf8(path));
        if (!text) continue;
        std::size_t count = 0;
        (void)renamed(*text, from, to, &count, out.kept_names);
        if (count) out.edits.emplace_back(path, count);
    }
    return out;
}

std::expected<void, std::string> apply(const std::filesystem::path& root, const Plan& plan, std::string_view from, std::string_view to) {
    for (const auto& [path, count] : plan.edits) {
        const auto file = root / mcpplibs::os::fs::from_utf8(path);
        const auto text = mcpplibs::os::fs::read(file);
        if (!text) return std::unexpected(std::format("{}: {}", path, text.error().message()));
        if (auto written = mcpplibs::os::fs::write_atomic(file, renamed(*text, from, to, nullptr, plan.kept_names)); !written) return std::unexpected(std::format("{}: {}", path, written.error().message()));
    }
    for (const auto& [path, moved] : plan.moves) {
        std::error_code error;
        std::filesystem::create_directories((root / mcpplibs::os::fs::from_utf8(moved)).parent_path(), error);
        auto ran = run::program(root, "git", {"mv", path, moved});
        if (!ran || ran->status != 0) return std::unexpected(std::format("git mv {} {} failed", path, moved));
    }
    // The directories the moves left empty go too, deepest first.
    for (const auto& [path, moved] : plan.moves)
        for (auto directory = (root / mcpp_path(path)).parent_path(); directory != root && directory.has_relative_path(); directory = directory.parent_path()) {
            std::error_code error;
            if (!std::filesystem::is_empty(directory, error) || error) break;
            std::filesystem::remove(directory, error);
        }
    return {};
}

}  // namespace omui::devtools::rename
