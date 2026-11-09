// omui.gen.data: JSON snapshots read, data modules evaluated into them.
module omui.gen.data;

import std;
import mlibs.json;
import nlohmann.json;
import omui.lang.ts;
import omui.gen.model;

namespace omui::gen {

bool is_data_module(const std::filesystem::path& file) { return generic_utf8(file.filename()).ends_with(".omui.ts"); }

std::expected<mlibs::json::Document, std::string> json_document(const std::filesystem::path& file) {
    const auto name = generic_utf8(file);
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("{}: cannot be read", name));
    auto document = mlibs::json::Document::parse(std::string{std::istreambuf_iterator<char>(in), {}}, name);
    if (!document) return std::unexpected(document.error().text());
    if (!document->root().is_object()) return std::unexpected(std::format("{}: not a JSON object", name));
    return std::move(*document);
}

std::expected<Snapshot, std::string> snapshot(const std::filesystem::path& file) {
    auto data = lang::ts::evaluate(file);
    if (!data) return std::unexpected(data.error());
    const auto name = generic_utf8(file.filename());
    return Snapshot{name.substr(0, name.size() - std::string_view(".ts").size()) + ".json", std::format("{{\"{}\":{}}}\n", data->kind, data->json)};
}

std::expected<mlibs::json::Document, std::string> data_document(const std::filesystem::path& file, std::string_view kind) {
    auto data = lang::ts::evaluate(file);
    if (!data) return std::unexpected(data.error());
    const auto name = generic_utf8(file.filename());
    const std::string function = std::format("define{}{}", static_cast<char>(kind[0] - 'a' + 'A'), kind.substr(1));
    if (data->kind != kind) return std::unexpected(std::format("{}: a {} file's default export is {}(…), not a {}", name, kind, function, data->kind));
    if (kind != "ops") {
        // One component (or native module) a file, named as the file: the build plans by the name alone.
        const nlohmann::ordered_json value = nlohmann::ordered_json::parse(data->json, nullptr, false);
        const std::string suffix = std::format(".{}.omui.ts", kind);
        const auto expected = name.ends_with(suffix) ? name.substr(0, name.size() - suffix.size()) : name;
        if (value.is_discarded() || !value.is_object() || value.size() != 1 || !value.contains(expected))
            return std::unexpected(std::format("{}: a {} file holds one, named as the file: {}({{ {}: {{ … }} }})", name, kind, function, expected));
    }
    const auto path = generic_utf8(file);  // the snapshot is named as its module, .json for .ts
    auto document = mlibs::json::Document::parse(std::format("{{\"{}\":{}}}", kind, data->json), path.substr(0, path.size() - 3) + ".json");
    if (!document) return std::unexpected(document.error().text());
    return std::move(*document);
}

}  // namespace omui::gen
