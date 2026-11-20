// mlibs.json's schema check: a document held to a JSON Schema (draft 2020-12), the keywords the openmui
// specification's schemas use. Each failure is a problem at the instance's pointer; where a choice of
// schemas (anyOf, oneOf) has none that fits, the problem says so once, not each branch's reasons.
module mlibs.json;

import std;
import nlohmann.json;

namespace mlibs::json {

namespace {

constexpr std::array notes{"title", "description", "$comment", "default", "examples", "format", "$schema", "$id", "$defs", "definitions",
                           "deprecated", "readOnly", "writeOnly", "contentMediaType", "contentEncoding"};

struct Failure {
    Node at;
    std::string message;
};

class Checker {
public:
    explicit Checker(const Document& schema) : schema_(schema) {}

    // The failures of `value` against `rule` (stops early when `first_only`: a branch of anyOf needs one).
    void check(const Node& rule, const Node& value, std::vector<Failure>& out, bool first_only = false) {
        if (first_only && !out.empty()) return;
        if (auto boolean = rule.boolean()) {
            if (!*boolean) out.push_back({value, "nothing is allowed here (the schema is false)"});
            return;
        }
        if (!rule.is_object()) return;
        for (const auto& [keyword, argument] : rule.members()) {
            if (first_only && !out.empty()) return;
            apply(keyword, argument, rule, value, out, first_only);
        }
    }

    std::vector<Problem> schema_problems;  // keywords this checker does not know

private:
    bool fits(const Node& rule, const Node& value) {
        std::vector<Failure> trial;
        check(rule, value, trial, true);
        return trial.empty();
    }

    const std::regex& pattern(const std::string& text) {
        auto found = patterns_.find(text);
        if (found == patterns_.end()) found = patterns_.emplace(text, std::regex(text, std::regex::ECMAScript)).first;
        return found->second;
    }

    static bool is_type(const Node& value, std::string_view type) {
        if (type == "object") return value.is_object();
        if (type == "array") return value.is_array();
        if (type == "string") return value.is_string();
        if (type == "boolean") return value.is_boolean();
        if (type == "null") return value.is_null();
        if (type == "number") return value.is_number();
        if (type == "integer") {
            if (value.is_integer()) return true;
            const auto number = value.number();
            return number && std::isfinite(*number) && std::floor(*number) == *number;
        }
        return false;
    }

    static std::size_t code_points(std::string_view text) {
        return static_cast<std::size_t>(std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
    }

    static std::string shown(const Node& value) {
        std::string text = value.dump();
        if (text.size() > 60) text = text.substr(0, 57) + "...";
        return text;
    }

    void apply(const std::string& keyword, const Node& argument, const Node& rule, const Node& value, std::vector<Failure>& out, bool first_only) {
        if (std::ranges::contains(notes, std::string_view(keyword))) return;
        if (keyword == "type") {
            std::vector<std::string> types;
            if (auto one = argument.text()) types.push_back(*one);
            else
                for (const auto& item : argument.items())
                    if (auto one = item.text()) types.push_back(*one);
            if (std::ranges::none_of(types, [&](const std::string& type) { return is_type(value, type); })) {
                std::string list;
                for (const auto& type : types) list += (list.empty() ? "" : ", ") + type;
                out.push_back({value, std::format("{} is not of type {}", shown(value), list)});
            }
        } else if (keyword == "enum") {
            const std::string text = value.dump();
            if (std::ranges::none_of(argument.items(), [&](const Node& item) { return item.dump() == text; }))
                out.push_back({value, std::format("{} is not one of {}", shown(value), shown(argument))});
        } else if (keyword == "const") {
            if (value.dump() != argument.dump()) out.push_back({value, std::format("{} was expected", shown(argument))});
        } else if (keyword == "$ref") {
            const auto reference = argument.text().value_or("");
            if (!reference.starts_with('#')) {
                schema_problems.push_back(schema_.problem(argument, std::format("a reference outside the schema ({}) is not followed", reference)));
                return;
            }
            const Node target = schema_.root().at(reference.substr(1));
            if (!target) {
                schema_problems.push_back(schema_.problem(argument, std::format("{} names nothing in the schema", reference)));
                return;
            }
            check(target, value, out, first_only);
        } else if (keyword == "properties") {
            if (!value.is_object()) return;
            for (const auto& [name, inner] : argument.members())
                if (const Node member = value[name]) check(inner, member, out, first_only);
        } else if (keyword == "patternProperties") {
            if (!value.is_object()) return;
            for (const auto& [expression, inner] : argument.members())
                for (const auto& [name, member] : value.members())
                    if (std::regex_search(name, pattern(expression))) check(inner, member, out, first_only);
        } else if (keyword == "additionalProperties") {
            if (!value.is_object()) return;
            const Node known = rule["properties"], patterned = rule["patternProperties"];
            for (const auto& [name, member] : value.members()) {
                if (known && known[name]) continue;
                bool matched = false;
                if (patterned)
                    for (const auto& [expression, _] : patterned.members()) matched = matched || std::regex_search(name, pattern(expression));
                if (matched) continue;
                if (argument.boolean() == false) out.push_back({member, std::format("'{}' is not an allowed property", name)});
                else check(argument, member, out, first_only);
            }
        } else if (keyword == "required") {
            if (!value.is_object()) return;
            for (const auto& item : argument.items())
                if (auto name = item.text(); name && !value[*name]) out.push_back({value, std::format("'{}' is a required property", *name)});
        } else if (keyword == "dependentRequired") {
            if (!value.is_object()) return;
            for (const auto& [name, needed] : argument.members())
                if (value[name])
                    for (const auto& item : needed.items())
                        if (auto other = item.text(); other && !value[*other])
                            out.push_back({value, std::format("'{}' is a dependency of '{}'", *other, name)});
        } else if (keyword == "propertyNames") {
            if (!value.is_object()) return;
            for (const auto& [name, member] : value.members()) {
                // The name as a string value: checked against the rule through a scratch document.
                auto scratch = Document::parse(nlohmann::ordered_json(name).dump(), "");
                if (!scratch) continue;
                std::vector<Failure> named;
                check(argument, scratch->root(), named, true);
                if (!named.empty()) out.push_back({member, std::format("'{}' is not an allowed property name ({})", name, named.front().message)});
            }
        } else if (keyword == "items") {
            if (!value.is_array()) return;
            const std::size_t skip = rule["prefixItems"] ? rule["prefixItems"].size() : 0;
            const auto items = value.items();
            for (std::size_t at = skip; at < items.size(); ++at) {
                if (argument.boolean() == false) out.push_back({items[at], "no item is allowed here"});
                else check(argument, items[at], out, first_only);
            }
        } else if (keyword == "prefixItems") {
            if (!value.is_array()) return;
            const auto rules = argument.items();
            const auto items = value.items();
            for (std::size_t at = 0; at < std::min(rules.size(), items.size()); ++at) check(rules[at], items[at], out, first_only);
        } else if (keyword == "minItems" || keyword == "maxItems") {
            if (!value.is_array()) return;
            const auto bound = static_cast<std::size_t>(argument.integer().value_or(0));
            if (keyword == "minItems" ? value.size() < bound : value.size() > bound)
                out.push_back({value, std::format("{} has {} {} items ({} {})", shown(value), keyword == "minItems" ? "fewer than" : "more than", bound,
                                                  value.size(), "items")});
        } else if (keyword == "uniqueItems") {
            if (!value.is_array() || argument.boolean() != true) return;
            std::set<std::string> seen;
            for (const auto& item : value.items())
                if (!seen.insert(item.dump()).second) out.push_back({value, std::format("{} has non-unique elements", shown(value))});
        } else if (keyword == "minLength" || keyword == "maxLength") {
            const auto text = value.text();
            if (!text) return;
            const auto bound = static_cast<std::size_t>(argument.integer().value_or(0));
            const std::size_t length = code_points(*text);
            if (keyword == "minLength" ? length < bound : length > bound)
                out.push_back({value, std::format("{} is too {}", shown(value), keyword == "minLength" ? "short" : "long")});
        } else if (keyword == "pattern") {
            const auto text = value.text();
            if (text && !std::regex_search(*text, pattern(argument.text().value_or(""))))
                out.push_back({value, std::format("{} does not match '{}'", shown(value), argument.text().value_or(""))});
        } else if (keyword == "minProperties" || keyword == "maxProperties") {
            if (!value.is_object()) return;
            const auto bound = static_cast<std::size_t>(argument.integer().value_or(0));
            if (keyword == "minProperties" ? value.size() < bound : value.size() > bound)
                out.push_back({value, std::format("{} has too {} properties", shown(value), keyword == "minProperties" ? "few" : "many")});
        } else if (keyword == "minimum" || keyword == "maximum" || keyword == "exclusiveMinimum" || keyword == "exclusiveMaximum") {
            const auto number = value.is_number() ? value.number() : std::nullopt;
            const auto bound = argument.number();
            if (!number || !bound) return;
            const bool fails = keyword == "minimum" ? *number < *bound
                             : keyword == "maximum" ? *number > *bound
                             : keyword == "exclusiveMinimum" ? *number <= *bound
                                                             : *number >= *bound;
            if (fails) out.push_back({value, std::format("{} is outside {} {}", shown(value), keyword, *bound)});
        } else if (keyword == "multipleOf") {
            const auto number = value.is_number() ? value.number() : std::nullopt;
            const auto step = argument.number();
            if (number && step && *step != 0) {
                const double quotient = *number / *step;
                if (std::abs(quotient - std::round(quotient)) > 1e-9) out.push_back({value, std::format("{} is not a multiple of {}", shown(value), *step)});
            }
        } else if (keyword == "allOf") {
            for (const auto& inner : argument.items()) check(inner, value, out, first_only);
        } else if (keyword == "anyOf") {
            if (std::ranges::none_of(argument.items(), [&](const Node& inner) { return fits(inner, value); }))
                out.push_back({value, std::format("{} is not valid under any of the given schemas", shown(value))});
        } else if (keyword == "oneOf") {
            const auto count = std::ranges::count_if(argument.items(), [&](const Node& inner) { return fits(inner, value); });
            if (count != 1)
                out.push_back({value, count == 0 ? std::format("{} is not valid under any of the given schemas", shown(value))
                                                 : std::format("{} is valid under each of {} of the given schemas", shown(value), count)});
        } else if (keyword == "not") {
            if (fits(argument, value)) out.push_back({value, std::format("{} should not be valid under {}", shown(value), shown(argument))});
        } else if (keyword == "if") {
            const Node chosen = fits(argument, value) ? rule["then"] : rule["else"];
            if (chosen) check(chosen, value, out, first_only);
        } else if (keyword == "then" || keyword == "else") {
            // read with `if`
        } else {
            const std::string where = argument.pointer();
            if (unknown_.insert(where).second)
                schema_problems.push_back(schema_.problem(argument, std::format("`{}` is a keyword this check does not know", keyword)));
        }
    }

    const Document& schema_;
    std::map<std::string, std::regex> patterns_;
    std::set<std::string> unknown_;
};

}  // namespace

std::vector<Problem> validate(const Document& schema, const Document& instance) {
    Checker checker(schema);
    std::vector<Failure> failures;
    checker.check(schema.root(), instance.root(), failures);
    std::vector<Problem> out = std::move(checker.schema_problems);
    std::ranges::stable_sort(failures, {}, [](const Failure& failure) { return failure.at.pointer(); });
    for (auto& failure : failures) out.push_back(instance.problem(failure.at, std::move(failure.message)));
    return out;
}

}  // namespace mlibs::json
