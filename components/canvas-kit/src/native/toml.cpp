// Reading *.behavior.omui.toml: toml++ reports a malformed file by throwing,
// which this unit — the kit's only use of it — turns into a problem, and
// hands the rest on as a plain tree with positions.
module openmui.ui.canvas_kit;

import std;
import tomlplusplus;
import :chart;

namespace omui::kit {

namespace {

Toml convert(const toml::node& n) {
    Toml t;
    const auto s = n.source();
    t.line = static_cast<int>(s.begin.line);
    t.column = static_cast<int>(s.begin.column);
    if (const toml::table* table = n.as_table()) {
        t.type = Toml::Type::table;
        for (const auto& [k, v] : *table) {
            t.keys.emplace_back(k.str());
            t.items.push_back(convert(v));
        }
    } else if (const toml::array* array = n.as_array()) {
        t.type = Toml::Type::array;
        for (const toml::node& v : *array) t.items.push_back(convert(v));
    } else if (auto text = n.value_exact<std::string>()) {
        t.type = Toml::Type::text;
        t.text = *text;
    } else if (auto i = n.value_exact<std::int64_t>()) {
        t.type = Toml::Type::integer;
        t.number = static_cast<double>(*i);
    } else if (auto d = n.value_exact<double>()) {
        t.type = Toml::Type::real;
        t.number = *d;
    } else if (auto b = n.value_exact<bool>()) {
        t.type = Toml::Type::boolean;
        t.boolean = *b;
    } else {
        t.type = Toml::Type::other;  // dates and times: nothing here takes them
    }
    return t;
}

}  // namespace

std::expected<Toml, world::BehaviorProblem> parse_toml(std::string_view text, std::string_view file) {
    try {
        const toml::table root = toml::parse(text, std::string(file));
        Toml t = convert(root);
        t.line = t.column = 1;
        return t;
    } catch (const toml::parse_error& e) {
        const auto s = e.source();
        return std::unexpected(world::BehaviorProblem{std::string(file), static_cast<int>(s.begin.line), static_cast<int>(s.begin.column),
                                                      std::string(e.description()), ""});
    }
}

}  // namespace omui::kit
