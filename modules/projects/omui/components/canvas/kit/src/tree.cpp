// Reading a behaviour file: the JSON a `*.behavior.omui.ts` evaluates to
// (D-139), handed on as a plain tree with each value's place.
module omui.ui.canvas.kit;

import std;
import mlibs.json;
import :chart;

namespace omui::kit {

namespace {

Tree convert(const mlibs::json::Document& doc, const mlibs::json::Node& n) {
    Tree t;
    const auto place = doc.problem(n, {});
    t.line = static_cast<int>(place.line);
    t.column = static_cast<int>(place.column);
    if (n.is_object()) {
        t.type = Tree::Type::table;
        // By key, as the charts have always been read: a file's order changes no behaviour.
        for (const auto& [key, value] : n.members_by_key()) {
            t.keys.push_back(key);
            t.items.push_back(convert(doc, value));
        }
    } else if (n.is_array()) {
        t.type = Tree::Type::array;
        for (const mlibs::json::Node& value : n.items()) t.items.push_back(convert(doc, value));
    } else if (auto text = n.text()) {
        t.type = Tree::Type::text;
        t.text = *text;
    } else if (auto whole = n.integer()) {
        t.type = Tree::Type::integer;
        t.number = static_cast<double>(*whole);
    } else if (auto real = n.number()) {
        t.type = Tree::Type::real;
        t.number = *real;
    } else if (auto boolean = n.boolean()) {
        t.type = Tree::Type::boolean;
        t.boolean = *boolean;
    } else {
        t.type = Tree::Type::other;  // null: nothing here takes it
    }
    return t;
}

}  // namespace

std::expected<Tree, world::BehaviorProblem> parse_tree(std::string_view text, std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) {
        const auto& p = doc.error();
        return std::unexpected(world::BehaviorProblem{p.file, static_cast<int>(p.line), static_cast<int>(p.column), p.message, ""});
    }
    return convert(*doc, doc->root());
}

}  // namespace omui::kit
