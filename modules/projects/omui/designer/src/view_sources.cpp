// omui.designer:view_sources (view_sources.cppm): view documents as a design's sources.
module omui.designer;

import std;
import mcpplibs.os;
import mlibs.hash;
import omui.viewdoc;

namespace omui::designer {

namespace {

constexpr std::string_view suffix = ".view.xdef.json";

std::string name_of(const std::filesystem::path& path) {
    const std::u8string text = path.filename().u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string joined(const std::vector<std::string>& names) {
    std::string out = "[";
    for (const std::string& name : names) out += std::format("{}\"{}\"", out.size() > 1 ? "," : "", name);
    return out + "]";
}

}  // namespace

std::map<std::string, std::string> ViewSources::texts() {
    std::map<std::string, std::string> out;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory_, error)) {
        const std::string name = name_of(entry.path());
        if (!entry.is_regular_file() || !name.ends_with(suffix)) continue;
        if (auto text = mcpplibs::os::fs::read(entry.path())) out.emplace(name, std::move(*text));
    }
    return out;
}

std::expected<std::string, std::string> ViewSources::apply(const std::map<std::string, std::string>& texts, Reason) {
    for (const auto& [path, text] : texts) {
        if (!path.ends_with(suffix) || path.contains('/') || path.contains('\\'))
            return std::unexpected(std::format("{} is no view document of this directory (<name>{})", path, suffix));
        const viewdoc::ReadResult read = viewdoc::read(text, {.file = path});
        if (!read.ok()) {
            std::string said;
            for (const viewdoc::Diagnostic& problem : read.diagnostics)
                if (problem.level == viewdoc::Diagnostic::Level::error) said += (said.empty() ? "" : "; ") + problem.text(path);
            return std::unexpected(said.empty() ? std::format("{} does not read", path) : said);
        }
    }
    const auto now = this->texts();
    std::vector<std::string> changed, removed;
    for (const auto& [path, text] : texts) {
        if (const auto there = now.find(path); there != now.end() && there->second == text) continue;
        if (auto written = mcpplibs::os::fs::write_atomic(directory_ / mcpplibs::os::fs::from_utf8(path), text); !written)
            return std::unexpected(std::format("{}: {}", path, written.error().message()));
        changed.push_back(path);
    }
    for (const auto& [path, text] : now) {
        if (texts.contains(path)) continue;
        std::error_code error;
        std::filesystem::remove(directory_ / mcpplibs::os::fs::from_utf8(path), error);
        removed.push_back(path);
    }
    return std::format(R"({{"changed":{},"removed":{}}})", joined(changed), joined(removed));
}

std::string ViewSources::state() {
    mlibs::hash::Fnv1a hash;
    for (const auto& [path, text] : texts()) {
        hash.str(path);
        const auto canonical = viewdoc::read(text).ok() ? viewdoc::format_document(text) : std::unexpected(viewdoc::Diagnostic{});
        hash.str(canonical ? *canonical : std::string("does not read"));
    }
    return std::format("{:016x}", hash.value());
}

}  // namespace omui::designer
