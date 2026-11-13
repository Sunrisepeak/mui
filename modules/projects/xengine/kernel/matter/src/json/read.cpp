// A definition from its JSON snapshot — what a *.matter.omui.ts data module evaluates to (D-139):
//
//   {"matter": {
//     "columns":   {"viscosity": {"unit": "…", "doc": "…", "least": 0, "most": 100, "default": 0, "fraction": 0}},
//     "channels":  {"pressure": {"unit": "…", "doc": "…", "initial": 0, "from": "…", "resets": false, "rise": "…", "fall": "…"}},
//     "materials": {"wood": {"phase": "solid", "density": 600, "ignition": 570, "fuel": 15, "burnt": "ash", "tags": ["plant"]}},
//     "laws":      [{"transfer": "temperature", "by": "thermal"},
//                   {"name": "burn", "when": "temperature >= ignition && burned < fuel && near.oxygen > 0",
//                    "add": {"burned": "burn_rate", "temperature": "heat"}, "flag": "burning"},
//                   {"when": "phase == liquid && temperature >= boiling", "become": "boiled", "event": "boil"}]
//   }}
//
// on the standard columns and channels. Every problem is said by its pointer and placed in the text.
module xengine.matter.json;

import std;
import mlibs.error;
import mlibs.json;
import mlibs.names;
import xengine.matter;
import :standard_snapshot;
import :convection_snapshot;

namespace xengine::matter {
namespace {

using mlibs::json::Document;
using mlibs::json::Node;
using mlibs::json::Problem;

struct Reader {
    const Document&      document;
    std::vector<Problem> problems;

    void wrong(const Node& at, std::string message, std::string hint = {}) { problems.push_back(document.problem(at, std::move(message), std::move(hint))); }
    // Only the keys a thing has.
    void keys(const Node& object, std::span<const std::string_view> allowed, std::string_view what) {
        for (const auto& [key, value] : object.members())
            if (std::ranges::find(allowed, key) == allowed.end()) wrong(value, std::format("{} has no key `{}`", what, key), mlibs::names::nearest(key, allowed));
    }
    [[nodiscard]] std::string text(const Node& node, std::string_view what) {
        if (!node) return {};
        if (const auto value = node.text()) return *value;
        wrong(node, std::format("{} is text", what));
        return {};
    }
    [[nodiscard]] std::optional<double> number(const Node& node, std::string_view what) {
        if (!node) return std::nullopt;
        if (const auto value = node.number()) return value;
        wrong(node, std::format("{} is a number", what));
        return std::nullopt;
    }
    [[nodiscard]] std::optional<std::uint32_t> whole(const Node& node, std::string_view what) {
        if (!node) return std::nullopt;
        const auto value = node.integer();
        if (value && *value >= 0 && *value <= std::numeric_limits<std::uint32_t>::max()) return static_cast<std::uint32_t>(*value);
        wrong(node, std::format("{} is a whole number, 0 … 4294967295", what));
        return std::nullopt;
    }
    // {"temperature": "heat", "burned": 1}: what changes, and by what amount (a sum as text, or a whole number).
    [[nodiscard]] std::vector<Effect> effects(const Node& node, std::string_view what) {
        std::vector<Effect> out;
        if (!node) return out;
        if (!node.is_object()) {
            wrong(node, std::format("`{}` is an object: what changes, and by how much", what));
            return out;
        }
        for (const auto& [target, amount] : node.members()) {
            if (const auto value = amount.text()) out.push_back({target, *value});
            else if (const auto count = amount.integer(); count && *count >= 0) out.push_back({target, std::to_string(*count)});
            else wrong(amount, std::format("the amount `{}` changes by is a sum as text (\"wetness * 40\") or a whole number", target));
        }
        return out;
    }
};

// The node a pointer of the definition names, as far as the document has it.
[[nodiscard]] Node node_at(Node from, std::string_view pointer) {
    Node at = from;
    while (!pointer.empty() && pointer.front() == '/') {
        pointer.remove_prefix(1);
        const std::size_t end = pointer.find('/');
        std::string token(pointer.substr(0, end));
        pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);
        for (std::size_t tilde = token.find('~'); tilde != std::string::npos; tilde = token.find('~', tilde + 1))
            if (tilde + 1 < token.size()) token.replace(tilde, 2, token[tilde + 1] == '1' ? "/" : "~");
        Node next;
        if (at.is_array()) {
            std::size_t index = 0;
            if (std::from_chars(token.data(), token.data() + token.size(), index).ec == std::errc{}) next = at[index];
        } else {
            next = at[token];
        }
        if (!next) break;
        at = next;
    }
    return at;
}

struct Parsed {
    Document   document;
    Definition definition;
};

[[nodiscard]] std::expected<Parsed, std::vector<Problem>> parse(std::string_view text, std::string_view file, bool pack = false) {
    auto document = Document::parse(text, std::string(file));
    if (!document) return std::unexpected(std::vector<Problem>{document.error()});
    Reader reader{*document, {}};
    const Node root = document->root()["matter"];
    if (!root || !root.is_object()) {
        reader.wrong(document->root(), "a matter definition is {\"matter\": {materials, laws …}}: what `defineMatter` evaluates to");
        return std::unexpected(std::move(reader.problems));
    }
    static constexpr std::array<std::string_view, 4> root_keys{"columns", "channels", "materials", "laws"};
    reader.keys(root, root_keys, "a matter definition");
    // A pack says only what it adds; a definition starts from the standard columns and channels.
    Definition definition = pack ? Definition{} : Definition::standard();

    if (const Node columns = root["columns"]) {
        if (!columns.is_object()) reader.wrong(columns, "`columns` is an object: each property by its name");
        for (const auto& [name, node] : columns.members()) {
            static constexpr std::array<std::string_view, 7> column_keys{"kind", "unit", "doc", "least", "most", "default", "fraction"};
            if (!node.is_object()) {
                reader.wrong(node, std::format("column `{}` is an object", name));
                continue;
            }
            reader.keys(node, column_keys, std::format("column `{}`", name));
            Column column{.name = name};
            const std::string kind = reader.text(node["kind"], "`kind`");
            static constexpr std::array<std::string_view, 3> kinds{"number", "phase", "material"};
            if (!kind.empty()) {
                const auto found = std::ranges::find(kinds, kind);
                if (found == kinds.end()) reader.wrong(node["kind"], std::format("`{}` is not a kind of column", kind), mlibs::names::nearest(kind, kinds));
                else column.kind = static_cast<ColumnKind>(found - kinds.begin());
            }
            column.unit = reader.text(node["unit"], "`unit`");
            column.doc = reader.text(node["doc"], "`doc`");
            if (column.kind == ColumnKind::material) {
                column.initial = static_cast<double>(none);
            } else if (column.kind == ColumnKind::phase) {
                reader.wrong(node, "a definition has one phase column, the standard `phase`");
            } else {
                column.least = reader.number(node["least"], "`least`").value_or(0);
                column.most = reader.number(node["most"], "`most`").value_or(4294967295.0);
                column.initial = reader.number(node["default"], "`default`").value_or(column.least);
                column.fraction = reader.whole(node["fraction"], "`fraction`").value_or(0);
            }
            definition.columns.push_back(std::move(column));
        }
    }
    if (const Node channels = root["channels"]) {
        if (!channels.is_object()) reader.wrong(channels, "`channels` is an object: each channel by its name");
        for (const auto& [name, node] : channels.members()) {
            static constexpr std::array<std::string_view, 8> channel_keys{"unit", "doc", "initial", "fraction", "from", "resets", "rise", "fall"};
            if (!node.is_object()) {
                reader.wrong(node, std::format("channel `{}` is an object", name));
                continue;
            }
            reader.keys(node, channel_keys, std::format("channel `{}`", name));
            ChannelSpec channel{.name = name};
            channel.unit = reader.text(node["unit"], "`unit`");
            channel.doc = reader.text(node["doc"], "`doc`");
            channel.initial = reader.number(node["initial"], "`initial`").value_or(0);
            channel.fraction = reader.whole(node["fraction"], "`fraction`").value_or(0);
            channel.from = reader.text(node["from"], "`from`");
            if (const Node resets = node["resets"]) {
                if (const auto value = resets.boolean()) channel.resets = *value;
                else reader.wrong(resets, "`resets` is true or false");
            }
            channel.rise = reader.text(node["rise"], "`rise`");
            channel.fall = reader.text(node["fall"], "`fall`");
            definition.channels.push_back(std::move(channel));
        }
    }
    const Node materials = root["materials"];
    if (materials && !materials.is_object()) reader.wrong(materials, "`materials` is an object: each material by its name");
    for (const auto& [name, node] : materials.members()) {
        if (!node.is_object()) {
            reader.wrong(node, std::format("material `{}` is an object: its properties", name));
            continue;
        }
        MaterialSpec material{.name = name};
        for (const auto& [key, value] : node.members()) {
            if (key == "tags") {
                if (!value.is_array()) reader.wrong(value, "`tags` is a list of words");
                for (const Node& tag : value.items()) {
                    if (const auto word = tag.text()) material.tags.push_back(*word);
                    else reader.wrong(tag, "a tag is a word");
                }
            } else if (const auto number = value.number()) {
                material.properties.push_back({key, *number});
            } else if (const auto word = value.text()) {
                material.properties.push_back({key, *word});
            } else {
                reader.wrong(value, std::format("`{}` of `{}` is a number, or the name of a phase or a material", key, name));
            }
        }
        definition.materials.push_back(std::move(material));
    }
    if (const Node laws = root["laws"]) {
        if (!laws.is_array()) reader.wrong(laws, "`laws` is a list");
        for (const Node& node : laws.items()) {
            static constexpr std::array<std::string_view, 16> law_keys{"name", "when", "transfer", "by",   "multiply", "shift", "add",   "take",
                                                                       "set",  "raise", "lower",   "flag", "become",   "event", "recipe", "move"};
            if (!node.is_object()) {
                reader.wrong(node, "a law is an object");
                definition.laws.emplace_back();  // keeps the laws' places
                continue;
            }
            reader.keys(node, law_keys, "a law");
            LawSpec law;
            law.name = reader.text(node["name"], "`name`");
            law.when = reader.text(node["when"], "`when`");
            law.transfer = reader.text(node["transfer"], "`transfer`");
            law.by = reader.text(node["by"], "`by`");
            law.multiply = reader.whole(node["multiply"], "`multiply`").value_or(1);
            law.shift = reader.whole(node["shift"], "`shift`").value_or(6);
            law.add = reader.effects(node["add"], "add");
            law.take = reader.effects(node["take"], "take");
            law.set = reader.effects(node["set"], "set");
            law.raise = reader.effects(node["raise"], "raise");
            law.lower = reader.effects(node["lower"], "lower");
            law.flag = reader.text(node["flag"], "`flag`");
            law.become = reader.text(node["become"], "`become`");
            law.event = reader.text(node["event"], "`event`");
            if (const Node recipe = node["recipe"]) {
                if (const auto value = recipe.boolean()) law.recipe = *value;
                else reader.wrong(recipe, "`recipe` is true or false");
            }
            if (const Node move = node["move"]) {
                static constexpr std::array<std::string_view, 8> move_keys{"down", "up", "by", "from", "speed", "force", "toward", "accel"};
                if (!move.is_object()) reader.wrong(move, "`move` is an object");
                else {
                    reader.keys(move, move_keys, "a motion");
                    law.move.said = true;
                    law.move.down = reader.text(move["down"], "`down`");
                    law.move.up = reader.text(move["up"], "`up`");
                    law.move.by = reader.text(move["by"], "`by`");
                    law.move.from = reader.number(move["from"], "`from`").value_or(0);
                    law.move.speed = reader.number(move["speed"], "`speed`").value_or(0);
                    law.move.force = reader.number(move["force"], "`force`").value_or(0);
                    law.move.accel = reader.number(move["accel"], "`accel`").value_or(0);
                    if (const Node toward = move["toward"]) {
                        const std::vector<Node> parts = toward.is_array() ? toward.items() : std::vector<Node>{};
                        if (parts.size() != 2) reader.wrong(toward, "`toward` is a direction: [x, y]");
                        else {
                            law.move.toward = true;
                            law.move.toward_x = reader.number(parts[0], "`toward`'s x").value_or(0);
                            law.move.toward_y = reader.number(parts[1], "`toward`'s y").value_or(0);
                        }
                    }
                }
            }
            definition.laws.push_back(std::move(law));
        }
    }
    if (!reader.problems.empty()) return std::unexpected(std::move(reader.problems));
    return Parsed{std::move(*document), std::move(definition)};
}

}  // namespace

std::expected<Definition, std::vector<mlibs::json::Problem>> read(std::string_view text, std::string_view file) {
    auto parsed = parse(text, file);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    return std::move(parsed->definition);
}

std::expected<Program, std::vector<mlibs::json::Problem>> prepare_document(std::string_view text, std::string_view file, Limits limits) {
    auto parsed = parse(text, file);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    auto program = Program::prepare(parsed->definition, limits);
    if (program) return std::move(*program);
    // Said where it is written: the pointer's node, or the nearest thing above it the text has.
    const Failure& failure = program.error();
    const Node at = node_at(parsed->document.root()["matter"], failure.where);
    Problem problem = parsed->document.problem(at, failure.message);
    problem.at = "/matter" + failure.where;
    return std::unexpected(std::vector<Problem>{std::move(problem)});
}

std::expected<Definition, std::vector<mlibs::json::Problem>> read_pack(std::string_view text, std::string_view file) {
    auto parsed = parse(text, file, true);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    return std::move(parsed->definition);
}

std::expected<Program, std::string> standard_with(std::span<const Definition> packs, Limits limits) {
    auto definition = read(standard_snapshot, "standard.matter.omui.json");
    if (!definition) return std::unexpected(std::string("the standard definition was refused"));
    for (const Definition& pack : packs) {
        auto laid = overlay(std::move(*definition), pack);
        if (!laid) return std::unexpected(said(laid.error()));
        *definition = std::move(*laid);
    }
    auto program = Program::prepare(*definition, limits);
    if (!program) return std::unexpected(std::format("{} at {}", program.error().message, program.error().where));
    return std::move(*program);
}

std::expected<Program, std::vector<mlibs::json::Problem>> standard() { return prepare_document(standard_snapshot, "standard.matter.omui.json"); }
std::expected<Definition, std::vector<mlibs::json::Problem>> convection() { return read_pack(convection_snapshot, "convection.matter.omui.json"); }

}  // namespace xengine::matter
