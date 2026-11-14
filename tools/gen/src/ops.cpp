module omui.gen.ops;

import std;
import mlibs.json;
import omui.gen.data;

namespace omui::gen {
namespace {
constexpr std::array effects{std::string_view("read"), std::string_view("reversible"),
                             std::string_view("irreversible")};
constexpr std::array costs{std::string_view("instant"), std::string_view("frame"),
                           std::string_view("bounded"), std::string_view("heavy")};
constexpr std::array scalar_types{std::string_view("bool"),   std::string_view("integer"),
                                  std::string_view("number"), std::string_view("seconds"),
                                  std::string_view("string"), std::string_view("json")};

bool dotted_name(std::string_view name) {
    if (name.empty() || name.find('.') == std::string_view::npos || name.front() == '.' ||
        name.back() == '.')
        return false;
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
    });
}
bool type_name(std::string_view type) {
    if (type.empty())
        return false;
    if (std::ranges::find(scalar_types, type) != scalar_types.end())
        return true;
    // A named kind (a handle or a structured value): lowercase words joined by '-'.
    return std::ranges::all_of(type, [](char c) { return (c >= 'a' && c <= 'z') || c == '-'; });
}
std::string cpp_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}
std::string json_string(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}
std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) out += (out.empty() ? "" : " ") + word;
    return out;
}
std::string identifier(std::string_view name) {
    std::string out;
    for (const char c : name)
        out += (c == '.' || c == '-') ? '_' : c;
    return out;
}
std::string effect_enum(std::string_view effect) {
    return "openas::Effect::" + std::string(effect);
}
std::string schema_type(std::string_view type, std::string_view doc) {
    std::string kind = "\"string\"", description(doc);
    if (type == "bool")
        kind = "\"boolean\"";
    else if (type == "integer")
        kind = "\"integer\"";
    else if (type == "number" || type == "seconds")
        kind = "\"number\"";
    else if (type == "json")
        kind.clear();
    else if (type == "path")
        description = std::format("a file or directory path{}{}", doc.empty() ? "" : ": ", doc);
    else if (type != "string")
        description = std::format("a {} (its id){}{}", type, doc.empty() ? "" : ": ", doc);
    if (type == "seconds")
        description = std::format("seconds{}{}", doc.empty() ? "" : ": ", doc);
    std::string schema = "{";
    if (!kind.empty())
        schema += "\"type\": " + kind;
    if (!description.empty())
        schema += std::format("{}\"description\": {}", kind.empty() ? "" : ", ",
                              json_string(description));
    return schema + "}";
}
}  // namespace

namespace {

using mlibs::json::Node;

// A value's text, or `fallback` when it is not text (absent).
std::string text_or(const Node& n, std::string fallback = {}) { return n.text().value_or(std::move(fallback)); }
bool flag_or(const Node& n, bool fallback = false) { return n.boolean().value_or(fallback); }

std::expected<Catalog, std::vector<std::string>> read_root(const mlibs::json::Document& doc, std::string_view file) {
    std::vector<std::string> problems;
    const auto at = [&](const Node& node) { return static_cast<int>(doc.problem(node, {}).line); };
    const auto problem = [&](int line, std::string message) {
        problems.push_back(std::format("{}:{}: {}", file, line, std::move(message)));
    };
    Catalog catalog;
    const Node head = doc.root()["ops"];
    if (!head.is_object())
        problem(1, "no `ops` object: { \"ops\": { product, module, namespace, doc, operations, tiers } }, as defineOps evaluates");
    else {
        catalog.product = text_or(head["product"]);
        catalog.module = text_or(head["module"]);
        catalog.ns = text_or(head["namespace"]);
        catalog.doc = text_or(head["doc"]);
        catalog.generated = text_or(head["generated"], "src/control");
        catalog.partition = text_or(head["partition"], "catalog");
        if (catalog.product.empty() || catalog.module.empty() != catalog.ns.empty())
            problem(at(head), "the catalog names its product, and the module holding its table "
                              "with that table's namespace (or neither)");
    }
    std::set<std::string> names;
    if (const Node list = head["operations"]; list.is_array()) {
        for (const Node& entry : list.items()) {
            const int line = at(entry);
            if (!entry.is_object()) {
                problem(line, "an operation is an object");
                continue;
            }
            Operation operation;
            operation.line = line;
            operation.name = text_or(entry["name"]);
            operation.effect = text_or(entry["effect"]);
            operation.summary = text_or(entry["summary"]);
            operation.result = text_or(entry["result"]);
            operation.cost = text_or(entry["cost"], "instant");
            operation.undo = text_or(entry["undo"]);
            operation.person = flag_or(entry["person"]);
            operation.cancellable = flag_or(entry["cancellable"]);
            operation.delegated = flag_or(entry["delegated"]);
            operation.undoable = flag_or(entry["undoable"]);
            if (!dotted_name(operation.name))
                problem(line, std::format("`{}` is not a dotted lowercase name (domain.verb)",
                                          operation.name));
            else if (!names.insert(operation.name).second)
                problem(line, std::format("`{}` is in the catalog twice", operation.name));
            if (std::ranges::find(effects, std::string_view(operation.effect)) == effects.end())
                problem(line, std::format("`{}`: effect `{}` is not read, reversible or "
                                          "irreversible (openas)",
                                          operation.name, operation.effect));
            if (std::ranges::find(costs, std::string_view(operation.cost)) == costs.end())
                problem(line, std::format("`{}`: cost `{}` is not instant, frame, bounded or heavy",
                                          operation.name, operation.cost));
            if (operation.summary.empty())
                problem(line, std::format("`{}` says what it does (summary)", operation.name));
            if (operation.delegated && operation.effect == "read")
                problem(line, std::format("`{}` runs what it names: its effect is the most that "
                                          "can be (not read)",
                                          operation.name));
            if (operation.undoable && operation.effect != "reversible")
                problem(line, std::format("`{}`: only a reversible operation is in the history "
                                          "(undoable)",
                                          operation.name));
            if (operation.effect == "read" && !operation.undo.empty())
                problem(line, std::format("`{}` reads: nothing to undo", operation.name));
            if (operation.effect != "read" && operation.undo.empty())
                problem(line, std::format("`{}` changes something: say how it is undone (undo; "
                                          "`none: why` when it cannot be)",
                                          operation.name));
            if (!operation.result.empty() && !type_name(operation.result))
                problem(line,
                        std::format("`{}`: result type `{}`", operation.name, operation.result));
            for (const Node& parameter : entry["params"].items()) {
                    if (!parameter.is_object()) {
                        problem(at(parameter), "a parameter is { name, type, doc }");
                        continue;
                    }
                    OpParameter value{text_or(parameter["name"]), text_or(parameter["type"]), text_or(parameter["doc"]), flag_or(parameter["optional"]), text_or(parameter["space"])};
                    if (!value.space.empty() && value.space != "plane" && value.space != "lattice-2d")
                        problem(at(parameter), std::format("`{}`: parameter `{}` has space `{}` (plane, lattice-2d)", operation.name, value.name, value.space));
                    if (value.name.empty() || !type_name(value.type))
                        problem(at(parameter),
                                std::format("`{}`: parameter `{}` needs a name and a type (bool, "
                                            "integer, number, seconds, string, json or a kind)",
                                            operation.name, value.name));
                    operation.parameters.push_back(std::move(value));
                }
            catalog.operations.push_back(std::move(operation));
        }
    }
    if (catalog.operations.empty())
        problem(1, "a catalog lists at least one operation");
    // A request as it is sent: the operation, then the arguments as written. Each argument is one of the
    // operation's parameters, and none it needs is left out — so what the catalog shows is accepted as shown.
    const auto request_of = [&](const Operation& operation, const Node& arguments, std::string_view where) {
        std::string line = std::format("{{\"op\":{}", json_string(operation.name));
        for (const auto& [key, value] : arguments.members()) {
            if (key == "op") continue;
            if (std::ranges::none_of(operation.parameters, [&](const OpParameter& parameter) { return parameter.name == key; }))
                problem(at(value), std::format("{}: `{}` has no parameter `{}`", where, operation.name, key));
            line += std::format(",{}:{}", json_string(key), value.dump());
        }
        for (const OpParameter& parameter : operation.parameters)
            if (!parameter.optional && !arguments[parameter.name])
                problem(at(arguments), std::format("{}: `{}` needs `{}`", where, operation.name, parameter.name));
        return line + "}";
    };
    {
        std::size_t index = 0;
        for (const Node& entry : head["operations"].items()) {
            if (!entry.is_object()) continue;
            Operation& operation = catalog.operations[index++];
            for (const Node& keyword : entry["keywords"].items()) {
                const std::string word = text_or(keyword);
                if (word.empty() || !std::ranges::all_of(word, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }))
                    problem(at(keyword), std::format("`{}`: a keyword is one lowercase word (`{}`)", operation.name, word));
                operation.keywords.push_back(word);
            }
            for (const Node& example : entry["examples"].items()) {
                if (!example.is_object()) {
                    problem(at(example), std::format("`{}`: an example is its arguments, an object ({{}} for none)", operation.name));
                    continue;
                }
                operation.examples.push_back(request_of(operation, example, "an example"));
            }
        }
    }
    std::set<std::string> task_names;
    for (const Node& entry : head["tasks"].items()) {
        const int line = at(entry);
        if (!entry.is_object()) {
            problem(line, "a task is { name, summary, steps }");
            continue;
        }
        Task task{text_or(entry["name"]), text_or(entry["summary"]), {}, line};
        if (task.name.empty() || !std::ranges::all_of(task.name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }))
            problem(line, std::format("a task's name is lowercase words joined by `-` (`{}`)", task.name));
        else if (!task_names.insert(task.name).second)
            problem(line, std::format("task `{}` is in the catalog twice", task.name));
        if (task.summary.empty())
            problem(line, std::format("task `{}` says what it gets done (summary)", task.name));
        for (const Node& step : entry["steps"].items()) {
            const std::string name = text_or(step["op"]);
            const auto operation = std::ranges::find_if(catalog.operations, [&](const Operation& listed) { return listed.name == name; });
            if (!step.is_object() || operation == catalog.operations.end()) {
                problem(at(step), std::format("task `{}`: a step is a request of the catalog, {{ op, … }} (`{}` is not one of its operations)", task.name, name));
                continue;
            }
            task.steps.push_back(request_of(*operation, step, std::format("task `{}`", task.name)));
        }
        if (task.steps.empty())
            problem(line, std::format("task `{}` has the requests that do it (steps)", task.name));
        catalog.tasks.push_back(std::move(task));
    }
    // An operation that shows no request of its own shows the first step of a task that is one: written once.
    for (Operation& operation : catalog.operations) {
        if (!operation.examples.empty()) continue;
        const std::string begins = std::format("{{\"op\":{}", json_string(operation.name));
        for (const Task& task : catalog.tasks) {
            const auto step = std::ranges::find_if(task.steps, [&](const std::string& line) { return line.starts_with(begins + ",") || line == begins + "}"; });
            if (step == task.steps.end()) continue;
            operation.examples.push_back(*step);
            break;
        }
    }
    for (const Node& entry : head["tiers"].items()) {
            const int line = at(entry);
            if (!entry.is_object()) {
                problem(line, "a tier is an object");
                continue;
            }
            Tier tier;
            tier.line = line;
            tier.name = text_or(entry["name"]);
            tier.view = text_or(entry["view"], "drawn");
            tier.doc = text_or(entry["doc"]);
            tier.reaction_ms = static_cast<int>(entry["reaction_ms"].integer().value_or(0));
            tier.actions_per_second = static_cast<int>(entry["actions_per_second"].integer().value_or(0));
            if (tier.name.empty())
                problem(line, "a tier has a name");
            if (tier.view != "drawn" && tier.view != "full")
                problem(line,
                        std::format("tier `{}`: view `{}` is drawn or full", tier.name, tier.view));
            if (tier.reaction_ms < 0 || tier.actions_per_second < 0)
                problem(line,
                        std::format("tier `{}`: reaction and rate are not negative", tier.name));
            for (const Node& item : entry["operations"].items()) {
                    const std::string name = text_or(item);
                    if (!names.contains(name))
                        problem(at(item),
                                std::format("tier `{}` uses `{}`, not an operation of the catalog",
                                            tier.name, name));
                    tier.operations.push_back(name);
                }
            if (tier.operations.empty())
                problem(line, std::format("tier `{}` names the operations it uses", tier.name));
            catalog.tiers.push_back(std::move(tier));
        }
    if (!problems.empty())
        return std::unexpected(std::move(problems));
    return catalog;
}

}  // namespace

std::expected<Catalog, std::vector<std::string>> read_catalog(std::string_view text,
                                                              std::string_view file) {
    auto doc = mlibs::json::Document::parse(text, std::string(file));
    if (!doc) return std::unexpected(std::vector<std::string>{doc.error().text()});
    return read_root(*doc, file);
}

std::expected<Catalog, std::vector<std::string>> read_catalog_file(const std::filesystem::path& file) {
    std::string name;  // UTF-8, never through the code page
    for (const char8_t unit : file.filename().generic_u8string()) name += static_cast<char>(unit);
    if (is_data_module(file)) {
        auto doc = data_document(file, "ops");
        if (!doc) return std::unexpected(std::vector<std::string>{doc.error()});
        return read_root(*doc, name);
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::vector<std::string>{std::format("{}: cannot be read", name)});
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return read_catalog(text, name);  // its JSON snapshot
}

std::string emit_catalog_cpp(const Catalog& catalog, std::string_view source) {
    std::string out = std::format("// Generated by omui-gen ops from {} — do not edit.\n"
                                  "// {}'s operation catalog (convergence plan §10.3).\n"
                                  "export module {}:{};\n\nimport std;\nimport "
                                  "mlibs.control;\n\nexport namespace {} {{\n\n",
                                  source, catalog.product, catalog.module, catalog.partition, catalog.ns);
    for (const auto& operation : catalog.operations) {
        if (operation.parameters.empty())
            continue;
        out += std::
            format("inline constexpr std::array<mlibs::control::Parameter, {}> parameters_{}{{{{\n",
                   operation.parameters.size(), identifier(operation.name));
        for (const auto& parameter : operation.parameters)
            out += std::format("    {{{}, {}, {}, {}}},\n", cpp_string(parameter.name),
                               cpp_string(parameter.type), cpp_string(parameter.doc),
                               parameter.optional ? "true" : "false");
        out += "}};\n";
    }
    for (const auto& operation : catalog.operations) {
        if (operation.examples.empty())
            continue;
        out += std::format("inline constexpr std::array<std::string_view, {}> examples_{}{{{{\n", operation.examples.size(), identifier(operation.name));
        for (const auto& example : operation.examples)
            out += std::format("    {},\n", cpp_string(example));
        out += "}};\n";
    }
    out += std::format("\n// {}\ninline constexpr std::array<mlibs::control::CatalogEntry, {}> "
                       "catalog{{{{\n",
                       catalog.doc, catalog.operations.size());
    for (const auto& operation : catalog.operations)
        out +=
            std::format("    {{{{{}, {}, {}}}, {}, {}, {}, mlibs::control::Cost::{}, {}, {}, {}, {}, {}{}}},\n",
                        cpp_string(operation.name), effect_enum(operation.effect),
                        operation.person ? "true" : "false", cpp_string(operation.summary),
                        operation.parameters.empty() ? std::string("{}")
                                                     : "parameters_" + identifier(operation.name),
                        cpp_string(operation.result), operation.cost, cpp_string(operation.undo),
                        operation.cancellable ? "true" : "false",
                        operation.undoable ? "true" : "false",
                        operation.delegated ? "true" : "false",
                        operation.examples.empty() ? std::string("{}") : "examples_" + identifier(operation.name),
                        operation.keywords.empty() ? std::string() : ", " + cpp_string(joined(operation.keywords)));
    out += "}};\n";
    // What an agent that knows nothing of it begins with (D-186): what it is, and its tasks.
    for (const auto& task : catalog.tasks) {
        out += std::format("\ninline constexpr std::array<std::string_view, {}> steps_{}{{{{\n", task.steps.size(), identifier(task.name));
        for (const auto& step : task.steps)
            out += std::format("    {},\n", cpp_string(step));
        out += "}};";
    }
    if (!catalog.tasks.empty()) {
        out += std::format("\ninline constexpr std::array<mlibs::control::Task, {}> guide_tasks{{{{\n", catalog.tasks.size());
        for (const auto& task : catalog.tasks)
            out += std::format("    {{{}, {}, steps_{}}},\n", cpp_string(task.name), cpp_string(task.summary), identifier(task.name));
        out += "}};";
    }
    out += std::format("\ninline constexpr mlibs::control::Guide guide{{{}, {}}};\n", cpp_string(catalog.doc), catalog.tasks.empty() ? "{}" : "guide_tasks");
    // The listing itself, for a program that serves the catalog (MCP) without its file.
    out += std::format("\n// The listing ({}.ops.json), as written beside this file.\n"
                       "inline constexpr std::string_view listing = R\"listing({})listing\";\n\n}}  // namespace {}\n",
                       catalog.product, emit_catalog_json(catalog), catalog.ns);
    return out;
}

std::string emit_catalog_json(const Catalog& catalog) {
    std::string out = std::format("{{\n  \"kind\": \"omui.ops\",\n  \"version\": 1,\n  "
                                  "\"product\": {},\n  \"doc\": {},\n  \"tools\": [\n",
                                  json_string(catalog.product), json_string(catalog.doc));
    for (std::size_t index = 0; index < catalog.operations.size(); ++index) {
        const auto& operation = catalog.operations[index];
        std::string properties, required;
        for (const auto& parameter : operation.parameters) {
            std::string schema = schema_type(parameter.type, parameter.doc);
            // A position says the form it is written in (`x-space`): an agent reads it from the listing.
            if (!parameter.space.empty() && schema.ends_with('}'))
                schema.insert(schema.size() - 1, std::format("{}\"x-space\": {}", schema.size() > 2 ? ", " : "", json_string(parameter.space)));
            properties += std::format("{}{}: {}", properties.empty() ? "" : ", ",
                                      json_string(parameter.name), schema);
            if (!parameter.optional)
                required +=
                    std::format("{}{}", required.empty() ? "" : ", ", json_string(parameter.name));
        }
        // Requests of it, as they are sent.
        std::string examples;
        for (const auto& example : operation.examples)
            examples += std::format("{}{}", examples.empty() ? "\n     \"examples\": [" : ", ", example);
        if (!examples.empty())
            examples += "],";
        if (!operation.keywords.empty()) {
            std::string words;
            for (const auto& word : operation.keywords)
                words += std::format("{}{}", words.empty() ? "" : ", ", json_string(word));
            examples += std::format("\n     \"keywords\": [{}],", words);
        }
        out += std::format("    {{\"name\": {}, \"description\": {}, \"effect\": {}, \"person\": "
                           "{}, \"delegated\": {}, \"cost\": {}, \"undo\": {}, "
                           "\"undoable\": {}, \"cancellable\": {}, \"result\": {},{}\n     \"inputSchema\": {{\"type\": \"object\", "
                           "\"properties\": {{{}}}, \"required\": [{}]}}}}{}\n",
                           json_string(operation.name), json_string(operation.summary),
                           json_string(operation.effect), operation.person ? "true" : "false",
                           operation.delegated ? "true" : "false", json_string(operation.cost), json_string(operation.undo),
                           operation.undoable ? "true" : "false", operation.cancellable ? "true" : "false", json_string(operation.result),
                           examples, properties, required, index + 1 < catalog.operations.size() ? "," : "");
    }
    out += "  ],\n  \"tiers\": [";
    for (std::size_t index = 0; index < catalog.tiers.size(); ++index) {
        const auto& tier = catalog.tiers[index];
        std::string operations;
        for (const auto& name : tier.operations)
            operations += std::format("{}{}", operations.empty() ? "" : ", ", json_string(name));
        out += std::format("{}\n    {{\"name\": {}, \"view\": {}, \"reaction_ms\": {}, "
                           "\"actions_per_second\": {}, \"doc\": {}, \"operations\": [{}]}}",
                           index ? "," : "", json_string(tier.name), json_string(tier.view),
                           tier.reaction_ms, tier.actions_per_second, json_string(tier.doc),
                           operations);
    }
    out += catalog.tiers.empty() ? "]" : "\n  ]";
    // The tasks to begin with: each step a request as it is sent.
    if (!catalog.tasks.empty()) {
        out += ",\n  \"tasks\": [";
        for (std::size_t index = 0; index < catalog.tasks.size(); ++index) {
            const auto& task = catalog.tasks[index];
            std::string steps;
            for (const auto& step : task.steps)
                steps += std::format("{}\n      {}", steps.empty() ? "" : ",", step);
            out += std::format("{}\n    {{\"name\": {}, \"summary\": {}, \"steps\": [{}\n    ]}}", index ? "," : "", json_string(task.name),
                               json_string(task.summary), steps);
        }
        out += "\n  ]";
    }
    return out + "\n}\n";
}

std::string emit_catalog_markdown(const Catalog& catalog) {
    std::string out = std::format("# {} operations\n\n{}\n\nGenerated by `omui-gen ops` from the "
                                  "catalog; the effect is openas's.\n\n"
                                  "| Operation | Effect | Parameters | Result | Cost | Undo | "
                                  "Cancel |\n|---|---|---|---|---|---|---|\n",
                                  catalog.product, catalog.doc);
    for (const auto& operation : catalog.operations) {
        std::string parameters;
        for (const auto& parameter : operation.parameters)
            parameters +=
                std::format("{}`{}`: {}{}{}", parameters.empty() ? "" : ", ", parameter.name,
                            parameter.type, parameter.space.empty() ? std::string() : std::format(" in {}", parameter.space), parameter.optional ? " (optional)" : "");
        out +=
            std::format("| `{}` — {} | {}{} | {} | {} | {} | {} | {} |\n", operation.name,
                        operation.summary,
                        operation.delegated ? "at most " + operation.effect + ", as what it runs"
                                            : operation.effect,
                        operation.person ? " (a person)" : "",
                        parameters.empty() ? "—" : parameters,
                        operation.result.empty() ? "—" : operation.result, operation.cost,
                        operation.undo.empty() ? "—" : operation.undo,
                        operation.cancellable ? "yes" : "no");
    }
    if (!catalog.tasks.empty()) {
        out += "\n## Tasks to begin with\n\nEach step is a request as it is sent; `describe` gives the same to an agent.\n";
        for (const auto& task : catalog.tasks) {
            out += std::format("\n**{}** — {}\n\n```json\n", task.name, task.summary);
            for (const auto& step : task.steps)
                out += step + "\n";
            out += "```\n";
        }
    }
    if (!catalog.tiers.empty()) {
        out += "\n## Agent player tiers\n\n| Tier | Shown | Reaction | Actions a second | "
               "Operations |\n|---|---|---|---|---|\n";
        for (const auto& tier : catalog.tiers) {
            std::string operations;
            for (const auto& name : tier.operations)
                operations += std::format("{}`{}`", operations.empty() ? "" : ", ", name);
            out +=
                std::format("| `{}` — {} | {} | {} ms | {} | {} |\n", tier.name, tier.doc,
                            tier.view == "full" ? "everything" : "what is drawn", tier.reaction_ms,
                            tier.actions_per_second ? std::to_string(tier.actions_per_second)
                                                    : std::string("no limit"),
                            operations);
        }
    }
    return out;
}

}  // namespace omui::gen
