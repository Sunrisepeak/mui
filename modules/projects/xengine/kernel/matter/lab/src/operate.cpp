// The lab's operations (lab.ops.omui.ts): the design read, changed, tried, explained; its history and its
// claims, which are the agent kernel's.
module xengine.matter.lab;

import std;
import openas;
import nlohmann.json;
import mlibs.agent;
import mlibs.control;
import mlibs.hash;
import mlibs.json;
import xengine.cellular.matter.agent;
import :text;

namespace xengine::matter::lab {

namespace {

// A JSON object's text with one more member.
[[nodiscard]] std::string with(std::string object, std::string_view member) {
    if (object.size() < 2 || object.back() != '}') return object;
    object.pop_back();
    if (object.back() != '{') object += ',';
    object += member;
    object += '}';
    return object;
}

[[nodiscard]] std::size_t lines_of(std::string_view text) { return static_cast<std::size_t>(std::ranges::count(text, '\n')); }

// A material's row with properties set (null takes one away); made when the design has it not.
std::expected<void, std::string> set_material(Texts& texts, std::string_view name, const Json& properties) {
    if (!properties.is_object()) return std::unexpected("`properties` is {property: value, …}; null takes one away");
    const std::string path = material_path(name);
    Json row = texts.contains(path) ? parsed(texts.at(path)).value_or(Json::object()) : Json::object();
    if (!row.is_object()) row = Json::object();
    for (auto member = properties.begin(); member != properties.end(); ++member) {
        if (member.value().is_null()) row.erase(member.key());
        else row[member.key()] = member.value();
    }
    texts[path] = inline_text(row);
    return {};
}

[[nodiscard]] std::string claim_text(const mlibs::agent::Claim& claim, double now) {
    return std::format("{{\"part\":{},\"subtree\":{},\"by\":{},\"intent\":{},\"seconds\":{:.0f}}}", json_text(claim.part), claim.subtree, json_text(claim.actor), json_text(claim.intent),
                       std::max(0.0, claim.until - now));
}

// A part a claim may name: the sources' own, a law by name, or `materials` and `laws` for all of them.
[[nodiscard]] bool is_part(std::string_view part) {
    if (part == "materials" || part == "laws" || part == "look" || part == "scene" || part == "channels" || part == "columns") return true;
    if (part.starts_with("materials/")) return is_name(part.substr(10));
    return part.starts_with("laws/") && part.size() > 5;
}

// What happened in a world as it was played, counted by name (an event's, or `became`, `rose`, `fell`).
[[nodiscard]] std::map<std::string, std::size_t> happened(const cellular::matter_agent::World& world) {
    std::map<std::string, std::size_t> counts;
    for (const cellular::matter_agent::Record& record : world.matter->records())
        if (record.kind != cellular::matter_agent::RecordKind::placed && record.kind != cellular::matter_agent::RecordKind::applied) ++counts[world.matter->happened_as(record)];
    return counts;
}

}  // namespace

void Lab::operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) {
    // Whose request this is, while it is answered; what it placed and stepped written when it is.
    struct Acting {
        Lab&                 lab;
        const openas::Actor* before;
        ~Acting() {
            lab.settle();
            lab.acting_ = before;
        }
    } acting{*this, std::exchange(acting_, &request.actor)};
    const std::string actor = openas::format(request.actor);
    const double at = now();
    const auto invalid = [&](std::string_view why) { answer.fail(openas::error_code::invalid_argument, why); };
    const auto said = [&](std::string_view key) { return request[key].text(); };
    const auto flag = [&](std::string_view key) { return request[key].boolean().value_or(false); };
    const auto json_of = [&](std::string_view key) -> std::optional<Json> {
        const mlibs::json::Node node = request[key];
        return node ? parsed(node.dump()) : std::nullopt;
    };

    // A change of the sources made what runs, kept as a commit with its picture, answered with what it did.
    const auto change = [&](const Texts& next, std::string message, std::optional<std::int64_t> position = std::nullopt) {
        auto changed = change_to(next, position);
        if (!changed) return answer.fail(changed.error().code, changed.error().text);
        if (changed->starts_with("{\"changed\":[]")) return answer.data(*changed);
        auto commit = history_.commit(texts(), std::move(message), actor, state(), std::nullopt, *changed);
        if (!commit) return answer.fail(openas::error_code::failed, commit.error());
        operations_.shown_commit.clear();
        picture_head();
        answer.data(with(std::move(*changed), std::format("\"commit\":{}", operations_.said(*commit))));
    };
    const auto message_or = [&](std::string otherwise) { return said("intent").value_or(std::move(otherwise)); };

    if (operation == "lab.describe") {
        const Texts sources = texts();
        const auto head = history_.resolve("HEAD");
        const auto committed = head ? history_.texts_of(*head) : std::expected<Texts, std::string>(std::unexpected(std::string()));
        const bool uncommitted = !committed || *committed != sources;
        std::string listed = "[";
        for (const auto& [path, text] : sources)
            listed += std::format("{}{{\"path\":{},\"lines\":{},\"hash\":\"{:08x}\"}}", listed.size() > 1 ? "," : "", json_text(path), lines_of(text),
                                  static_cast<std::uint32_t>(mlibs::hash::Fnv1a().text(text).value()));
        listed += "]";
        std::string claimed = "[";
        for (const mlibs::agent::Claim& claim : claims_.held(at)) claimed += std::format("{}{}", claimed.size() > 1 ? "," : "", claim_text(claim, at));
        claimed += "]";
        const auto whole = [&](std::string_view sources_said) {
            return std::format("{{\"lab\":{},\"sources\":{},\"world\":{{\"shape\":[{},{}],\"filled\":{},\"steps\":{},\"frame\":{},\"materials\":{},\"laws\":{}}},"
                               "\"history\":{{\"branch\":{},\"commit\":{},\"uncommitted\":{}}},\"claims\":{},"
                               "\"beside\":\"matter.* reads and edits what is there and says why; field.* keeps time; frame.* is the picture\"}}",
                               json_text(utf8(directory_.filename())), sources_said, shape_.width, shape_.height, json_text(filled_), steps(), world_.field->frame(),
                               world_.matter->program().materials().size(), world_.matter->program().laws().size(), json_text(history_.head_branch()),
                               json_text(operations_.shown_now()), uncommitted, claimed);
        };
        std::string out = whole(listed);
        if (const auto budget = request["budget"].integer(); budget && *budget > 0 && out.size() > static_cast<std::size_t>(*budget)) {
            out = whole(std::to_string(sources.size()));
            if (out.size() > static_cast<std::size_t>(*budget)) out = std::format("{{\"sources\":{},\"steps\":{},\"uncommitted\":{}}}", sources.size(), steps(), uncommitted);
        }
        return answer.data(out);
    }
    if (operation == "lab.read") {
        const auto path = said("path");
        const Texts sources = texts();
        const auto found = path ? sources.find(*path) : sources.end();
        if (found == sources.end()) {
            std::string paths;
            for (const auto& [listed, text] : sources) paths += std::format("{}{}", paths.empty() ? "" : ", ", listed);
            return answer.fail(openas::error_code::not_found, std::format("`path` is one of the sources: {}", paths));
        }
        return answer.data(std::format("{{\"path\":{},\"lines\":{},\"text\":{}}}", json_text(found->first), lines_of(found->second), json_text(found->second)));
    }
    if (operation == "lab.edit") {
        const auto path = said("path");
        if (!path || source_of(*path) == Source::none)
            return invalid("`path` is a source's: materials/<name>.json, laws.json, look.json, scene.json, channels.json or columns.json");
        Texts next = texts();
        const bool there = next.contains(*path);
        if (flag("remove")) {
            if (!there) return answer.fail(openas::error_code::not_found, std::format("{} is not among the sources", *path));
            next.erase(*path);
            return change(next, message_or(std::format("{} removed", *path)));
        }
        const auto text = said("text");
        if (!text || text->empty()) return invalid("`text` is the source's whole text (or `remove`: true takes it away)");
        next[*path] = *text;
        return change(next, message_or(std::format("{} {}", *path, there ? "edited" : "added")));
    }
    if (operation == "lab.material") {
        const auto name = said("name");
        if (!name || !is_name(*name)) return invalid("`name` is the material's: a name a law can say — letters, digits and _");
        Texts next = texts();
        const std::string path = material_path(*name);
        const bool there = next.contains(path);
        if (flag("remove")) {
            if (!there) return answer.fail(openas::error_code::not_found, std::format("`{}` is no material of the design", *name));
            next.erase(path);
            return change(next, message_or(std::format("material `{}` removed", *name)));
        }
        const auto properties = json_of("properties");
        if (!properties) return invalid("`properties` is {property: value, …} (or `remove`: true takes the row away)");
        if (auto set = set_material(next, *name, *properties); !set) return invalid(set.error());
        std::string keys;
        for (auto member = properties->begin(); member != properties->end(); ++member) keys += std::format("{}{}", keys.empty() ? "" : ", ", member.key());
        return change(next, message_or(there ? std::format("material `{}`: {}", *name, keys) : std::format("material `{}` added", *name)));
    }
    if (operation == "lab.law") {
        const auto name = said("name");
        if (!name || name->empty()) return invalid("`name` is the law's");
        Texts next = texts();
        Json laws = next.contains("laws.json") ? parsed(next.at("laws.json")).value_or(Json::array()) : Json::array();
        if (!laws.is_array()) laws = Json::array();
        const auto found = std::ranges::find_if(laws, [&](const Json& law) {
            const auto named = law.find("name");
            return law.is_object() && named != law.end() && named->is_string() && named->get<std::string>() == *name;
        });
        if (flag("remove")) {
            if (found == laws.end()) return answer.fail(openas::error_code::not_found, std::format("`{}` is no law of the design", *name));
            laws.erase(found);
            next["laws.json"] = inline_text(laws);
            return change(next, message_or(std::format("law `{}` removed", *name)));
        }
        const auto law = json_of("law");
        if (!law || !law->is_object()) return invalid("`law` is the law without its name: {when, add | take | set | raise | become, …} or {transfer, by | shift} (or `remove`: true)");
        Json entry = Json::object();
        entry["name"] = *name;
        for (auto member = law->begin(); member != law->end(); ++member)
            if (member.key() != "name") entry[member.key()] = member.value();
        const bool there = found != laws.end();
        if (there) *found = std::move(entry);
        else laws.push_back(std::move(entry));
        next["laws.json"] = inline_text(laws);
        return change(next, message_or(std::format("law `{}` {}", *name, there ? "changed" : "added")));
    }
    if (operation == "lab.try") {
        Texts next = texts();
        if (const auto material = said("material")) {
            if (!is_name(*material)) return invalid("`material` is a material's name");
            const auto properties = json_of("properties");
            if (!properties) return invalid("`properties` is {property: value, …}: the row as it would be");
            if (auto set = set_material(next, *material, *properties); !set) return invalid(set.error());
        } else if (const auto path = said("path")) {
            const auto text = said("text");
            if (!text || text->empty()) return invalid("`text` is the source's whole text as it would be");
            next[*path] = *text;
        } else {
            return invalid("a change to try: `material` with `properties`, or `path` with `text`");
        }
        for (auto& [path, text] : next) {
            auto kept = normalized(path, text);
            if (!kept) return invalid(std::format("{} {}", path, kept.error()));
            text = std::move(*kept);
        }
        const std::int64_t more = request["steps"].integer().value_or(0);
        if (more < 0 || more > 4096) return invalid("`steps` is 0 … 4096 steps run on in both");
        const std::int64_t target = steps() + more;
        const auto here = build(texts(), target);
        if (!here) return answer.fail(openas::error_code::failed, here.error());
        const auto there = build(next, target);
        if (!there) return invalid(there.error());
        std::string out = std::format("{{\"steps\":{},{},\"happened\":{{", there->world.field->state().steps, difference(here->world, there->world));
        const auto now_happens = happened(here->world), would_happen = happened(there->world);
        std::set<std::string> names;
        for (const auto& [name, count] : now_happens) names.insert(name);
        for (const auto& [name, count] : would_happen) names.insert(name);
        bool first = true;
        for (const std::string& name : names) {
            out += std::format("{}{}:[{},{}]", first ? "" : ",", json_text(name), now_happens.contains(name) ? now_happens.at(name) : 0, would_happen.contains(name) ? would_happen.at(name) : 0);
            first = false;
        }
        return answer.data(out + "},\"reads\":\"[as it is, as it would be]: `differ` cells whose material is another, `materials` and `happened` those that are not the same\"}");
    }
    if (operation == "lab.reset") {
        Json scene = Json::object();
        const auto shape = json_of("shape");
        if (shape && (!shape->is_array() || shape->size() != 2)) return invalid("`shape` is [width, height] in cells");
        scene["shape"] = shape ? *shape : Json::array({shape_.width, shape_.height});
        scene["filled"] = said("filled").value_or(filled_);
        scene["edits"] = Json::array();
        Texts next = texts();
        next["scene.json"] = inline_text(scene);
        return change(next, message_or("the scene begun anew"), 0);
    }
    if (operation == "lab.explain") {
        const auto part = said("part");
        if (!part || !is_part(*part) || *part == "materials") return invalid("`part` is materials/<name>, laws (or laws/<name>), look or scene");
        const bool law = part->starts_with("laws/");
        const std::string path = law || *part == "laws" ? std::string("laws.json") : std::format("{}.json", *part);
        const auto property = said("property");
        const std::string needle = property ? std::format("{}:", json_text(*property)) : law ? std::format("\"name\": {}", json_text(part->substr(5))) : std::string();
        const std::int64_t limit = request["limit"].integer().value_or(10);
        if (limit < 1 || limit > 50) return invalid("`limit` is 1 … 50 commits");
        const Texts sources = texts();
        const std::string current = sources.contains(path) ? sources.at(path) : std::string();
        std::string out = std::format("{{\"part\":{},\"path\":{}", json_text(*part), json_text(path));
        if (current.empty()) out += ",\"removed\":true";
        if (!needle.empty() && !current.empty()) {
            std::size_t line = 1, begin = 0;
            bool found = false;
            while (begin < current.size() && !found) {
                const std::size_t end = std::min(current.find('\n', begin), current.size());
                const std::string_view text = std::string_view(current).substr(begin, end - begin);
                if (text.contains(needle)) {
                    const std::size_t indent = std::min(text.find_first_not_of(' '), text.size());
                    out += std::format(",\"now\":{{\"line\":{},\"text\":{}}}", line, json_text(text.substr(indent)));
                    found = true;
                }
                begin = end + 1;
                ++line;
            }
            if (!found) out += ",\"now\":null";
        }
        const std::string shown = operations_.shown_now();
        const auto text_at = [&](const std::string& commit) -> std::string {
            const auto kept = history_.texts_of(commit);
            return kept && kept->contains(path) ? kept->at(path) : std::string();
        };
        if (!shown.empty() && text_at(shown) != current) out += ",\"uncommitted\":true";
        out += ",\"changes\":[";
        std::int64_t answered = 0;
        for (const mlibs::agent::Commit& commit : history_.log(shown.empty() ? std::string("HEAD") : shown, 200)) {
            if (answered >= limit) break;
            const auto parent = commit.parents.empty() ? std::optional<mlibs::agent::Commit>() : [&]() -> std::optional<mlibs::agent::Commit> {
                auto found = history_.commit_of(commit.parents.front());
                return found ? std::optional(std::move(*found)) : std::nullopt;
            }();
            const auto object_in = [&](const mlibs::agent::Commit& of) { return of.files.contains(path) ? of.files.at(path) : std::string(); };
            if (object_in(commit) == (parent ? object_in(*parent) : std::string())) continue;
            const mlibs::agent::LineChange lines = mlibs::agent::changed_lines(parent ? text_at(parent->id) : std::string(), text_at(commit.id));
            if (!needle.empty() && !lines.before.contains(needle) && !lines.after.contains(needle)) continue;
            out += std::format("{}{{\"commit\":{},\"by\":{},\"message\":{},\"line\":{},\"before\":{},\"after\":{}}}", answered ? "," : "", json_text(commit.id), json_text(commit.actor),
                               json_text(commit.message), lines.line, json_text(lines.before), json_text(lines.after));
            ++answered;
        }
        out += "]";
        if (const auto claim = claims_.over(*part, {}, at)) out += std::format(",\"held\":{}", claim_text(*claim, at));
        return answer.data(out + "}");
    }
    if (operation == "lab.picture") {
        const std::int64_t scale = request["scale"].integer().value_or(0);
        if (scale < 0 || scale > 16) return invalid("`scale` is 1 … 16 pixels a cell");
        const auto bytes = png(static_cast<int>(scale));
        if (!bytes) return answer.fail(openas::error_code::failed, bytes.error());
        const auto file = history_.keep_picture(*bytes);
        if (!file) return answer.fail(openas::error_code::failed, file.error());
        const int cell = scale ? static_cast<int>(scale) : options_.scale;
        return answer.data(std::format("{{\"path\":{},\"size\":[{},{}],\"bytes\":{},\"steps\":{}}}", json_text(utf8(*file)), shape_.width * static_cast<std::uint32_t>(cell),
                                       shape_.height * static_cast<std::uint32_t>(cell), bytes->size(), steps()));
    }
    static constexpr std::array<std::string_view, 8> kept{"history", "commit", "branch", "switch", "revert", "diff", "merge", "tag"};
    if (operation.starts_with("lab.") && std::ranges::contains(kept, operation.substr(4))) {
        refused_ = openas::error_code::invalid_argument;
        auto ran = operations_.run(operation.substr(4), request.document->root(), actor);
        // The history says what kind of refusal its own was; what the sources refused is theirs (refused_).
        if (!ran) return answer.fail(operations_.refused_as.empty() ? refused_ : operations_.refused_as, ran.error());
        picture_head();
        return answer.data(*ran);
    }
    if (operation == "lab.claim") {
        const auto part = said("part");
        if (!part || !is_part(*part)) return invalid("`part` is materials/<name>, laws/<name>, look, scene — or `materials`, `laws` with `subtree` for all of them");
        const double seconds = std::clamp(request["seconds"].number().value_or(30.0), 1.0, 600.0);
        const auto made = claims_.claim({.part = *part, .property = {}, .subtree = flag("subtree"), .intent = said("intent").value_or(std::string()), .seconds = seconds}, actor, at);
        if (!made) return answer.fail("conflict", held_text(made.error()));
        return answer.data(claim_text(*made, at));
    }
    if (operation == "lab.release") {
        const auto part = said("part");
        if (!part) return invalid("`part` is the part, as it was claimed");
        const bool released = claims_.release(*part, {}, actor, request.actor.kind == openas::Kind::person);
        return answer.data(std::format("{{\"part\":{},\"released\":{}}}", json_text(*part), released));
    }
    if (operation == "lab.claims") {
        std::string out = "{\"claims\":[";
        bool first = true;
        for (const mlibs::agent::Claim& claim : claims_.held(at)) {
            out += std::format("{}{}", first ? "" : ",", claim_text(claim, at));
            first = false;
        }
        return answer.data(out + "]}");
    }
    answer.fail(openas::error_code::not_found, std::format("`{}` is no operation of the lab", operation));
}

}  // namespace xengine::matter::lab
