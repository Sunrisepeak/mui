// An appearance from its JSON snapshot — what a *.appearance.omui.ts data module evaluates to (D-139):
//
//   {"appearance": {
//     "phases":    {"granular": {"color": "#b9a377", "grain": 0.16}},
//     "materials": {"wood": {"color": "#8a5a2b", "grain": 0.12}},
//     "states":    [{"name": "charred", "by": "burned", "to": "fuel", "color": "#17120f", "mix": 0.85},
//                   {"name": "glows", "by": "temperature", "from": 800, "to": 1600, "color": "#ff5a14", "emits": true,
//                    "of": ["solid", "granular"]}],
//     "nothing":   "#00000000"
//   }}
//
// Every problem is said by its pointer and placed in the text.
module xengine.matter.appearance;

import std;
import mlibs.error;
import mlibs.json;
import mlibs.names;
import xengine.matter;
import :standard_appearance_snapshot;

namespace xengine::matter {
namespace {

using mlibs::json::Document;
using mlibs::json::Node;
using mlibs::json::Problem;

struct Reader {
    const Document&      document;
    std::vector<Problem> problems;

    void wrong(const Node& at, std::string message, std::string hint = {}) { problems.push_back(document.problem(at, std::move(message), std::move(hint))); }
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
    // {"wood": {"color": "#8a5a2b", "grain": 0.12}}: a base look by name.
    [[nodiscard]] std::vector<BaseSpec> bases(const Node& node, std::string_view what) {
        std::vector<BaseSpec> out;
        if (!node) return out;
        if (!node.is_object()) {
            wrong(node, std::format("`{}` is an object: each look by its name", what));
            return out;
        }
        static constexpr std::array<std::string_view, 2> base_keys{"color", "grain"};
        for (const auto& [name, look] : node.members()) {
            if (!look.is_object()) {
                wrong(look, std::format("the look of `{}` is an object: {{color, grain}}", name));
                continue;
            }
            keys(look, base_keys, std::format("the look of `{}`", name));
            out.push_back({name, text(look["color"], "`color`"), number(look["grain"], "`grain`")});
        }
        return out;
    }
};

// The node a pointer names, as far as the document has it.
[[nodiscard]] Node node_at(Node from, std::string_view pointer) {
    Node at = from;
    while (!pointer.empty() && pointer.front() == '/') {
        pointer.remove_prefix(1);
        const std::size_t end = pointer.find('/');
        const std::string token(pointer.substr(0, end));
        pointer = end == std::string_view::npos ? std::string_view{} : pointer.substr(end);
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
    Appearance appearance;
};

[[nodiscard]] std::expected<Parsed, std::vector<Problem>> parse(std::string_view text, std::string_view file) {
    auto document = Document::parse(text, std::string(file));
    if (!document) return std::unexpected(std::vector<Problem>{document.error()});
    Reader reader{*document, {}};
    const Node root = document->root()["appearance"];
    if (!root || !root.is_object()) {
        reader.wrong(document->root(), "an appearance is {\"appearance\": {phases, materials, states}}: what `defineAppearance` evaluates to");
        return std::unexpected(std::move(reader.problems));
    }
    static constexpr std::array<std::string_view, 4> root_keys{"phases", "materials", "states", "nothing"};
    reader.keys(root, root_keys, "an appearance");
    Appearance appearance;
    appearance.phases = reader.bases(root["phases"], "phases");
    appearance.materials = reader.bases(root["materials"], "materials");
    appearance.nothing = reader.text(root["nothing"], "`nothing`");
    if (const Node states = root["states"]) {
        if (!states.is_array()) reader.wrong(states, "`states` is a list");
        for (const Node& node : states.items()) {
            static constexpr std::array<std::string_view, 8> state_keys{"name", "by", "from", "to", "color", "mix", "emits", "of"};
            if (!node.is_object()) {
                reader.wrong(node, "a state is an object: {by, from, to, color …}");
                appearance.states.emplace_back();  // keeps the states' places
                continue;
            }
            reader.keys(node, state_keys, "a state");
            StateSpec state;
            state.name = reader.text(node["name"], "`name`");
            state.by = reader.text(node["by"], "`by`");
            state.from = reader.number(node["from"], "`from`").value_or(0);
            if (const Node to = node["to"]) {
                if (const auto value = to.number()) state.to = *value;
                else if (const auto name = to.text()) state.to = *name;
                else reader.wrong(to, "`to` is a number in the channel's unit, or a property of the thing itself (\"fuel\")");
            } else {
                reader.wrong(node, "a state shows along a range: `to` says where it is all there");
            }
            state.color = reader.text(node["color"], "`color`");
            if (!node["color"]) reader.wrong(node, "a state moves the colour toward one: `color`");
            state.mix = reader.number(node["mix"], "`mix`").value_or(1);
            if (const Node emits = node["emits"]) {
                if (const auto value = emits.boolean()) state.emits = *value;
                else reader.wrong(emits, "`emits` is true or false");
            }
            if (const Node of = node["of"]) {
                if (!of.is_array()) reader.wrong(of, "`of` is a list of phases");
                for (const Node& phase : of.items()) {
                    if (const auto word = phase.text()) state.of.push_back(*word);
                    else reader.wrong(phase, "a phase is a word: solid, soft, granular, liquid, gas");
                }
            }
            appearance.states.push_back(std::move(state));
        }
    }
    if (!reader.problems.empty()) return std::unexpected(std::move(reader.problems));
    return Parsed{std::move(*document), std::move(appearance)};
}

}  // namespace

std::expected<Appearance, std::vector<mlibs::json::Problem>> read_appearance(std::string_view text, std::string_view file) {
    auto parsed = parse(text, file);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    return std::move(parsed->appearance);
}

std::expected<Look, std::vector<mlibs::json::Problem>> look_document(const Program& program, std::string_view text, std::string_view file) {
    auto parsed = parse(text, file);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    auto look = Look::prepare(program, parsed->appearance);
    if (look) return std::move(*look);
    // Said where it is written: the pointer's node, or the nearest thing above it the text has.
    const Failure& failure = look.error();
    const Node at = node_at(parsed->document.root()["appearance"], failure.where);
    Problem problem = parsed->document.problem(at, failure.message);
    problem.at = "/appearance" + failure.where;
    return std::unexpected(std::vector<Problem>{std::move(problem)});
}

std::expected<Look, std::vector<mlibs::json::Problem>> standard_look(const Program& program) {
    return look_document(program, standard_appearance_snapshot, "standard.appearance.omui.json");
}

}  // namespace xengine::matter
