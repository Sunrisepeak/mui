// The lab's sources: their paths, the text they are kept as, and the snapshots they compose.
module xengine.matter.lab;

import std;
import nlohmann.json;
import mcpplibs.os;
import mlibs.agent;
import mlibs.json;
import xengine.matter.json;
import xengine.matter.appearance;
import :text;

namespace xengine::matter::lab {

namespace {

[[nodiscard]] std::string scalar_text(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }

void put_inline(const Json& value, std::string& out) {
    if (value.is_object()) {
        out += '{';
        bool first = true;
        for (auto member = value.begin(); member != value.end(); ++member) {
            if (!first) out += ", ";
            first = false;
            out += json_text(member.key());
            out += ": ";
            put_inline(member.value(), out);
        }
        out += '}';
    } else if (value.is_array()) {
        out += '[';
        bool first = true;
        for (const Json& element : value) {
            if (!first) out += ", ";
            first = false;
            put_inline(element, out);
        }
        out += ']';
    } else {
        out += scalar_text(value);
    }
}

// Whether a value holds rows: a member or an element that itself has members.
[[nodiscard]] bool holds_rows(const Json& value) {
    return std::ranges::any_of(value, [](const Json& inner) { return inner.is_structured() && !inner.empty(); });
}

void put_rows(const Json& value, int depth, int indent, std::string& out) {
    const bool broken = value.is_structured() && !value.empty() && depth > 0 && (indent == 0 || holds_rows(value));
    if (!broken) return put_inline(value, out);
    out += value.is_object() ? "{\n" : "[\n";
    std::size_t index = 0;
    for (auto member = value.begin(); member != value.end(); ++member, ++index) {
        out.append(static_cast<std::size_t>(indent) + 2, ' ');
        if (value.is_object()) {
            out += json_text(member.key());
            out += ": ";
        }
        put_rows(*member, depth - 1, indent + 2, out);
        out += index + 1 < value.size() ? ",\n" : "\n";
    }
    out.append(static_cast<std::size_t>(indent), ' ');
    out += value.is_object() ? '}' : ']';
}

[[nodiscard]] int depth_of(Source source) noexcept { return source == Source::look || source == Source::scene ? 2 : 1; }

// A source's value, or an empty one of its kind when the design has it not.
[[nodiscard]] Json value_of(const Texts& texts, std::string_view path, Json otherwise) {
    const auto found = texts.find(std::string(path));
    if (found == texts.end()) return otherwise;
    return parsed(found->second).value_or(std::move(otherwise));
}

}  // namespace

std::optional<Json> parsed(std::string_view text) {
    Json value = Json::parse(text, nullptr, false);
    if (value.is_discarded()) return std::nullopt;
    return value;
}

std::string inline_text(const Json& value) {
    std::string out;
    put_inline(value, out);
    return out;
}

std::string rows_text(const Json& value, int depth) {
    std::string out;
    put_rows(value, depth, 0, out);
    out += '\n';
    return out;
}

bool is_name(std::string_view text) noexcept {
    const auto letter = [](char symbol) { return (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z') || symbol == '_'; };
    if (text.empty() || text.size() > 64 || !letter(text.front())) return false;
    return std::ranges::all_of(text, [&](char symbol) { return letter(symbol) || (symbol >= '0' && symbol <= '9'); });
}

std::string utf8(const std::filesystem::path& path) { return mcpplibs::os::fs::generic_utf8(path); }

std::string_view material_of(std::string_view path) noexcept {
    constexpr std::string_view before = "materials/", after = ".json";
    if (!path.starts_with(before) || !path.ends_with(after) || path.size() <= before.size() + after.size()) return {};
    const std::string_view name = path.substr(before.size(), path.size() - before.size() - after.size());
    return is_name(name) ? name : std::string_view{};
}

std::string material_path(std::string_view name) { return std::format("materials/{}.json", name); }

Source source_of(std::string_view path) noexcept {
    if (!material_of(path).empty()) return Source::material;
    if (path == "laws.json") return Source::laws;
    if (path == "channels.json") return Source::channels;
    if (path == "columns.json") return Source::columns;
    if (path == "look.json") return Source::look;
    if (path == "scene.json") return Source::scene;
    return Source::none;
}

std::string part_of(std::string_view path) { return std::string(path.ends_with(".json") ? path.substr(0, path.size() - 5) : path); }

std::expected<std::string, std::string> normalized(std::string_view path, std::string_view text) {
    const Source source = source_of(path);
    if (source == Source::none)
        return std::unexpected("is no source of a lab: materials/<name>.json (a name a law can say: letters, digits, _), laws.json, look.json, scene.json, channels.json or columns.json");
    const auto value = parsed(text);
    if (!value) {
        const auto problem = mlibs::json::Document::parse(text, std::string(path));
        return std::unexpected(problem ? std::string("is no JSON") : std::format("is no JSON — line {}, column {}: {}", problem.error().line, problem.error().column, problem.error().message));
    }
    switch (source) {
        case Source::laws:
            if (!value->is_array() || !std::ranges::all_of(*value, [](const Json& law) { return law.is_object(); }))
                return std::unexpected("is the laws: a list, each law {name, when, …} or {name, transfer, …}");
            break;
        case Source::look:
            if (!value->is_object()) return std::unexpected("is the look: {phases, states, nothing}");
            if (value->contains("materials"))
                return std::unexpected("says `materials`: a material's look is in its own row — materials/<name>.json's `look` ({color, grain})");
            break;
        case Source::scene:
            if (!value->is_object() || !value->contains("shape") || !value->contains("filled"))
                return std::unexpected("is the scene: {shape: [width, height], filled: a material, edits: […]}");
            break;
        default:
            if (!value->is_object()) return std::unexpected("is a row: {property: value, …}");
            break;
    }
    return rows_text(*value, depth_of(source));
}

std::string definition_of(const Texts& texts) {
    Json matter = Json::object();
    if (texts.contains("columns.json")) matter["columns"] = value_of(texts, "columns.json", Json::object());
    if (texts.contains("channels.json")) matter["channels"] = value_of(texts, "channels.json", Json::object());
    Json materials = Json::object();
    for (const auto& [path, text] : texts) {
        const std::string_view name = material_of(path);
        if (name.empty()) continue;
        Json row = parsed(text).value_or(Json::object());
        if (row.is_object()) row.erase("look");
        materials[std::string(name)] = std::move(row);
    }
    matter["materials"] = std::move(materials);
    matter["laws"] = value_of(texts, "laws.json", Json::array());
    Json document = Json::object();
    document["matter"] = std::move(matter);
    return rows_text(document, 3);
}

std::string appearance_of(const Texts& texts) {
    Json appearance = value_of(texts, "look.json", Json::object());
    if (!appearance.is_object()) appearance = Json::object();
    Json materials = Json::object();
    for (const auto& [path, text] : texts) {
        const std::string_view name = material_of(path);
        if (name.empty()) continue;
        const Json row = parsed(text).value_or(Json::object());
        if (const auto look = row.find("look"); row.is_object() && look != row.end()) materials[std::string(name)] = *look;
    }
    if (!materials.empty()) appearance["materials"] = std::move(materials);
    Json document = Json::object();
    document["appearance"] = std::move(appearance);
    return rows_text(document, 3);
}

std::expected<Texts, std::string> sources_of(const Begin& begin) {
    const auto definition = parsed(begin.matter.empty() ? standard_snapshot : std::string_view(begin.matter));
    if (!definition || !definition->is_object() || !definition->contains("matter") || !(*definition)["matter"].is_object())
        return std::unexpected("the definition is no snapshot of one: {\"matter\": {materials, laws}} (what omui-gen snapshot writes of a *.matter.omui.ts)");
    const auto appearance = parsed(begin.look.empty() ? standard_appearance_snapshot : std::string_view(begin.look));
    if (!appearance || !appearance->is_object() || !appearance->contains("appearance") || !(*appearance)["appearance"].is_object())
        return std::unexpected("the look is no snapshot of one: {\"appearance\": {phases, materials, states}} (what omui-gen snapshot writes of a *.appearance.omui.ts)");
    const Json& matter = (*definition)["matter"];
    Json look = (*appearance)["appearance"];
    Json colours = Json::object();
    if (const auto found = look.find("materials"); found != look.end()) {
        if (found->is_object()) colours = *found;
        look.erase("materials");
    }

    Texts texts;
    const auto put = [&](std::string path, const Json& value) -> std::expected<void, std::string> {
        auto kept = normalized(path, inline_text(value));
        if (!kept) return std::unexpected(std::format("{} {}", path, kept.error()));
        texts.emplace(std::move(path), std::move(*kept));
        return {};
    };
    if (const auto materials = matter.find("materials"); materials != matter.end() && materials->is_object()) {
        for (auto material = materials->begin(); material != materials->end(); ++material) {
            if (!is_name(material.key())) return std::unexpected(std::format("`{}` is no name a law can say: letters, digits and _", material.key()));
            Json row = material.value();
            if (!row.is_object()) return std::unexpected(std::format("the material `{}` is no row of properties", material.key()));
            if (const auto colour = colours.find(material.key()); colour != colours.end()) row["look"] = *colour;
            if (auto done = put(material_path(material.key()), row); !done) return std::unexpected(done.error());
        }
    }
    if (auto done = put("laws.json", matter.contains("laws") ? matter["laws"] : Json::array()); !done) return std::unexpected(done.error());
    for (const std::string_view table : {std::string_view{"channels"}, std::string_view{"columns"}})
        if (const auto found = matter.find(std::string(table)); found != matter.end())
            if (auto done = put(std::format("{}.json", table), *found); !done) return std::unexpected(done.error());
    if (auto done = put("look.json", look); !done) return std::unexpected(done.error());
    Json scene = Json::object();
    scene["shape"] = Json::array({begin.shape.width, begin.shape.height});
    scene["filled"] = begin.filled;
    scene["edits"] = Json::array();
    if (auto done = put("scene.json", scene); !done) return std::unexpected(done.error());
    return texts;
}

}  // namespace xengine::matter::lab
