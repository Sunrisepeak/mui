// Reading a catalog: the JSON a `*.messages.omui.ts` evaluates to (D-139),
// `{"messages": {…}}`. Every message is parsed, and each problem keeps its
// line and column.
module mlibs.i18n.api;

import std;
import mlibs.json;

namespace mlibs::i18n {

namespace {

void collect(const mlibs::json::Document& doc, const mlibs::json::Node& t, const std::string& prefix, Catalog& out, std::vector<Diagnostic>& problems) {
    for (const auto& [k, node] : t.members()) {
        const std::string key = prefix.empty() ? k : std::format("{}.{}", prefix, k);
        const auto at = [&](std::string message) {
            const auto p = doc.problem(node, std::move(message));
            problems.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), std::format("{}: {}", p.at, p.message)});
        };
        if (node.is_object()) {
            collect(doc, node, key, out, problems);
        } else if (const auto text = node.text()) {
            if (auto r = out.add(key, *text); !r) at(r.error());
        } else {
            at(std::format("`{}` is not a message (a string) or an object of them", key));
        }
    }
}

}  // namespace

std::expected<Catalog, std::vector<Diagnostic>> read_messages(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) {
        const auto& p = doc.error();
        return std::unexpected(std::vector<Diagnostic>{{p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message}});
    }
    std::vector<Diagnostic> problems;
    const auto root = doc->root();
    for (const auto& [key, value] : root.members())
        if (key != "messages") {
            const auto p = doc->problem(value, std::format("`{}` is not a key of a catalog: {{\"messages\": {{…}}}}, as defineMessages evaluates", key));
            problems.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message});
        }
    Catalog out;
    if (!root["messages"].is_object()) {
        const auto p = doc->problem(root, "a catalog is {\"messages\": {…}}: keys whose values are messages, or objects of them");
        problems.push_back({p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message});
    } else {
        collect(*doc, root["messages"], "", out, problems);
    }
    if (!problems.empty()) return std::unexpected(std::move(problems));
    return out;
}

}  // namespace mlibs::i18n
