// Reading *.messages.omui.toml: toml++ reports a malformed file by throwing,
// which this unit — the library's adapter — turns into a diagnostic; every
// message is parsed, and each problem keeps its line and column.
module openmui.i18n;

import std;
import tomlplusplus;

namespace omui::i18n {

namespace {

void collect(const toml::table& t, const std::string& prefix, std::string_view file, Catalog& out, std::vector<Diagnostic>& problems) {
    for (const auto& [k, node] : t) {
        const std::string key = prefix.empty() ? std::string(k.str()) : std::format("{}.{}", prefix, k.str());
        const auto src = node.source();
        const int line = static_cast<int>(src.begin.line), column = static_cast<int>(src.begin.column);
        if (const toml::table* sub = node.as_table()) {
            collect(*sub, key, file, out, problems);
        } else if (const auto* s = node.as_string()) {
            if (auto r = out.add(key, s->get()); !r) problems.push_back({std::string(file), line, column, r.error()});
        } else {
            problems.push_back({std::string(file), line, column, std::format("`{}` is not a message (a string) or a table of them", key)});
        }
    }
}

}  // namespace

std::expected<Catalog, std::vector<Diagnostic>> read_messages(std::string_view text, std::string_view file) {
    std::vector<Diagnostic> problems;
    toml::table root;
    try {
        root = toml::parse(text, std::string(file));
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        problems.push_back({std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column), std::string(e.description())});
        return std::unexpected(std::move(problems));
    }
    Catalog out;
    collect(root, "", file, out, problems);
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace omui::i18n
