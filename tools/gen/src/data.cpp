// omui.gen.data: data modules evaluated, their JSON as TOML.
module omui.gen.data;

import std;
import tomlplusplus;
import nlohmann.json;
import omui.lang.ts;
import omui.gen.model;

namespace omui::gen {

namespace {

using Json = nlohmann::ordered_json;

// A JSON value as TOML text, inline, its keys in the module's order: parsed
// back, every node has a place (line, column) in that order, which is the
// order the readers take declarations in (a native module's arguments, its
// functions) — as a TOML file's own. Null has no TOML form.
std::expected<void, std::string> write(const Json& value, const std::string& pointer, std::string& out) {
    const auto key = [](std::string_view k) {
        const bool bare = !k.empty() && std::ranges::all_of(k, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; });
        return bare ? std::string(k) : Json(std::string(k)).dump();
    };
    switch (value.type()) {
    case Json::value_t::object: {
        out += '{';
        bool first = true;
        for (const auto& item : value.items()) {
            out += first ? " " : ", ";
            first = false;
            out += key(item.key()) + " = ";
            if (auto r = write(item.value(), std::format("{}/{}", pointer, item.key()), out); !r) return r;
        }
        out += first ? "}" : " }";
        return {};
    }
    case Json::value_t::array: {
        out += '[';
        std::size_t index = 0;
        for (const auto& item : value) {
            if (index) out += ", ";
            if (auto r = write(item, std::format("{}/{}", pointer, index++), out); !r) return r;
        }
        out += ']';
        return {};
    }
    case Json::value_t::string: out += Json(value.get<std::string>()).dump(); return {};  // a JSON string is a TOML basic string
    case Json::value_t::boolean: out += value.get<bool>() ? "true" : "false"; return {};
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: out += std::to_string(value.get<std::int64_t>()); return {};
    case Json::value_t::number_float: {
        std::string number = value.dump();
        if (number.find_first_of(".eE") == std::string::npos) number += ".0";
        out += number;
        return {};
    }
    default: return std::unexpected(std::format("{}: null has no place here (leave the key out)", pointer.empty() ? "/" : pointer));
    }
}

// A document's top-level tables, a line each, as TOML text; then parsed.
std::expected<toml::table, std::string> parsed(const std::vector<std::pair<std::string, const Json*>>& top, const std::string& name) {
    std::string text;
    for (const auto& [k, v] : top) {
        text += k + " = ";
        if (auto r = write(*v, "/" + k, text); !r) return std::unexpected(r.error());
        text += '\n';
    }
    try {
        return toml::parse(text, name);
    } catch (const toml::parse_error& error) {
        return std::unexpected(std::string(error.description()));
    }
}

}  // namespace

bool is_data_module(const std::filesystem::path& file) { return generic_utf8(file.filename()).ends_with(".omui.ts"); }

std::expected<toml::table, std::string> json_document(const std::filesystem::path& file) {
    const auto name = generic_utf8(file.filename());
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot be read", name));
    const Json value = Json::parse(std::string{std::istreambuf_iterator<char>(in), {}}, nullptr, false);
    if (value.is_discarded() || !value.is_object()) return std::unexpected(std::format("{}: not a JSON object", name));
    std::vector<std::pair<std::string, const Json*>> top;
    for (const auto& item : value.items()) top.emplace_back(item.key(), &item.value());
    auto document = parsed(top, name);
    if (!document) return std::unexpected(std::format("{}: {}", name, document.error()));
    return std::move(*document);
}

std::expected<toml::table, std::string> data_document(const std::filesystem::path& file, std::string_view kind) {
    auto data = lang::ts::evaluate(file);
    if (!data) return std::unexpected(data.error());
    const auto name = generic_utf8(file.filename());
    if (data->kind != kind)
        return std::unexpected(std::format("{}: a {} file's default export is define{}{}(…), not a {}", name, kind,
                                           static_cast<char>(kind[0] - 'a' + 'A'), kind.substr(1), data->kind));
    const Json value = Json::parse(data->json, nullptr, false);
    if (value.is_discarded()) return std::unexpected(std::format("{}: its JSON does not read back", name));
    auto problem = [&](std::string text) { return std::unexpected(std::format("{}: {}", name, std::move(text))); };
    if (kind == "ops") {
        // The catalog's fields, its operations and its tiers: [catalog], [[operation]], [[tier]].
        if (!value.is_object()) return problem("/: an object (product, doc, operations …)");
        Json head = Json::object();
        for (const auto& item : value.items())
            if (item.key() != "operations" && item.key() != "tiers") head[item.key()] = item.value();
        std::vector<std::pair<std::string, const Json*>> top{{"catalog", &head}};
        if (value.contains("operations")) top.emplace_back("operation", &value["operations"]);
        if (value.contains("tiers")) top.emplace_back("tier", &value["tiers"]);
        auto document = parsed(top, name);
        if (!document) return problem(document.error());
        return std::move(*document);
    }
    // One component (or native module) a file, named as the file: the build plans by the name alone.
    const std::string suffix = std::format(".{}.omui.ts", kind);
    const auto expected = name.ends_with(suffix) ? name.substr(0, name.size() - suffix.size()) : name;
    if (!value.is_object() || value.size() != 1 || !value.contains(expected))
        return problem(std::format("a {} file holds one, named as the file: define{}{}({{ {}: {{ … }} }})", kind,
                                   static_cast<char>(kind[0] - 'a' + 'A'), kind.substr(1), expected));
    auto document = parsed({{std::string(kind), &value}}, name);
    if (!document) return problem(document.error());
    return std::move(*document);
}

}  // namespace omui::gen
