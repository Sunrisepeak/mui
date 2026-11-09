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

// A JSON value as a TOML node; null has no TOML form.
std::expected<std::unique_ptr<toml::node>, std::string> node_of(const Json& value, const std::string& pointer) {
    switch (value.type()) {
    case Json::value_t::object: {
        auto table = std::make_unique<toml::table>();
        for (const auto& item : value.items()) {
            auto node = node_of(item.value(), std::format("{}/{}", pointer, item.key()));
            if (!node) return std::unexpected(node.error());
            table->insert(item.key(), std::move(**node));
        }
        return table;
    }
    case Json::value_t::array: {
        auto array = std::make_unique<toml::array>();
        std::size_t index = 0;
        for (const auto& item : value) {
            auto node = node_of(item, std::format("{}/{}", pointer, index++));
            if (!node) return std::unexpected(node.error());
            array->push_back(std::move(**node));
        }
        return array;
    }
    case Json::value_t::string: return std::make_unique<toml::value<std::string>>(value.get<std::string>());
    case Json::value_t::boolean: return std::make_unique<toml::value<bool>>(value.get<bool>());
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: return std::make_unique<toml::value<std::int64_t>>(value.get<std::int64_t>());
    case Json::value_t::number_float: return std::make_unique<toml::value<double>>(value.get<double>());
    default: return std::unexpected(std::format("{}: null has no place here (leave the key out)", pointer.empty() ? "/" : pointer));
    }
}

std::expected<toml::table, std::string> table_of(const Json& value, const std::string& pointer) {
    if (!value.is_object()) return std::unexpected(std::format("{}: an object, by name", pointer.empty() ? "/" : pointer));
    auto node = node_of(value, pointer);
    if (!node) return std::unexpected(node.error());
    return std::move(*(*node)->as_table());
}

}  // namespace

bool is_data_module(const std::filesystem::path& file) { return generic_utf8(file.filename()).ends_with(".omui.ts"); }

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
    toml::table document;
    if (kind == "ops") {
        // The catalog's fields, its operations and its tiers: [catalog], [[operation]], [[tier]].
        Json head = Json::object();
        if (!value.is_object()) return problem("/: an object (product, doc, operations …)");
        for (const auto& item : value.items())
            if (item.key() != "operations" && item.key() != "tiers") head[item.key()] = item.value();
        auto catalog = table_of(head, "");
        if (!catalog) return problem(catalog.error());
        document.insert("catalog", std::move(*catalog));
        for (const auto& [key, as] : std::array{std::pair{std::string_view("operations"), std::string_view("operation")},
                                                std::pair{std::string_view("tiers"), std::string_view("tier")}}) {
            if (!value.contains(key)) continue;
            auto list = node_of(value[std::string(key)], std::format("/{}", key));
            if (!list) return problem(list.error());
            document.insert(as, std::move(**list));
        }
        return document;
    }
    // One component (or native module) a file, named as the file: the build plans by the name alone.
    const std::string suffix = std::format(".{}.omui.ts", kind);
    const auto expected = name.ends_with(suffix) ? name.substr(0, name.size() - suffix.size()) : name;
    if (!value.is_object() || value.size() != 1 || !value.contains(expected))
        return problem(std::format("a {} file holds one, named as the file: define{}{}({{ {}: {{ … }} }})", kind,
                                   static_cast<char>(kind[0] - 'a' + 'A'), kind.substr(1), expected));
    auto body = table_of(value, "");
    if (!body) return problem(body.error());
    document.insert(std::string(kind), std::move(*body));
    return document;
}

}  // namespace omui::gen
