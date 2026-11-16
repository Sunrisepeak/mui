module omui.devtools.rename;

import std;
import mcpplibs.os;
import omui.devtools.run;

namespace omui::devtools::rename {

namespace {

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool code_name(std::string_view name) {
    return !name.empty() && std::islower(static_cast<unsigned char>(name.front())) &&
           std::ranges::all_of(name, [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)); });
}

// What is left as it was: the decisions and the record, under the names of their time; what is not text.
bool kept(std::string_view path) {
    if (path.starts_with(".agents/")) return true;
    constexpr std::array binary{".png", ".webp", ".jpg", ".glb", ".gltf", ".hdr", ".wasm", ".woff2", ".ttf", ".otf", ".wav", ".ogg", ".opus", ".webm", ".ktx2", ".bin", ".zip"};
    return std::ranges::any_of(binary, [&](std::string_view suffix) { return path.ends_with(suffix); });
}

std::expected<std::vector<std::string>, std::string> tracked(const std::filesystem::path& root) {
    auto listed = run::output(root, "git", {"ls-files", "-z"});
    if (!listed) return std::unexpected(listed.error());
    std::vector<std::string> paths;
    for (const auto part : std::views::split(*listed, '\0'))
        if (!part.empty()) paths.emplace_back(part.begin(), part.end());
    return paths;
}

}  // namespace

std::string renamed(std::string_view text, std::string_view from, std::string_view to, std::size_t* count) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0, found = 0;
    while (true) {
        const std::size_t next = text.find(from, at);
        if (next == std::string_view::npos) break;
        const bool before = next > 0 && word_char(text[next - 1]);
        const bool after = next + from.size() < text.size() && word_char(text[next + from.size()]);
        out.append(text.substr(at, next - at));
        if (before || after) out.append(from);
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
    for (const std::string& path : *paths) {
        if (kept(path)) continue;
        if (const std::string moved = renamed(path, from, to); moved != path) out.moves.emplace_back(path, moved);
        const auto text = mcpplibs::os::fs::read(root / mcpplibs::os::fs::from_utf8(path));
        if (!text) continue;
        std::size_t count = 0;
        (void)renamed(*text, from, to, &count);
        if (count) out.edits.emplace_back(path, count);
    }
    return out;
}

std::expected<void, std::string> apply(const std::filesystem::path& root, const Plan& plan, std::string_view from, std::string_view to) {
    for (const auto& [path, count] : plan.edits) {
        const auto file = root / mcpplibs::os::fs::from_utf8(path);
        const auto text = mcpplibs::os::fs::read(file);
        if (!text) return std::unexpected(std::format("{}: {}", path, text.error().message()));
        if (auto written = mcpplibs::os::fs::write_atomic(file, renamed(*text, from, to)); !written) return std::unexpected(std::format("{}: {}", path, written.error().message()));
    }
    for (const auto& [path, moved] : plan.moves) {
        std::error_code error;
        std::filesystem::create_directories((root / mcpplibs::os::fs::from_utf8(moved)).parent_path(), error);
        auto ran = run::program(root, "git", {"mv", path, moved});
        if (!ran || ran->status != 0) return std::unexpected(std::format("git mv {} {} failed", path, moved));
    }
    return {};
}

}  // namespace omui::devtools::rename
