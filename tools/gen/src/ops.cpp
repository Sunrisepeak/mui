module omui.gen.ops;

import std;
import tomlplusplus;

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
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
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

std::expected<Catalog, std::vector<std::string>> read_catalog(std::string_view text,
                                                              std::string_view file) {
    std::vector<std::string> problems;
    toml::table root;
    try {
        root = toml::parse(text, std::string(file));
    } catch (const toml::parse_error& error) {
        return std::unexpected(std::vector<std::string>{
            std::format("{}:{}: {}", file, error.source().begin.line, error.description())});
    }
    const auto at = [&](const toml::node* node) {
        return node ? static_cast<int>(node->source().begin.line) : 0;
    };
    const auto problem = [&](int line, std::string message) {
        problems.push_back(std::format("{}:{}: {}", file, line, std::move(message)));
    };
    Catalog catalog;
    const auto* head = root["catalog"].as_table();
    if (!head)
        problem(1, "no [catalog] (product, module, namespace, doc)");
    else {
        catalog.product = (*head)["product"].value_or(std::string());
        catalog.module = (*head)["module"].value_or(std::string());
        catalog.ns = (*head)["namespace"].value_or(std::string());
        catalog.doc = (*head)["doc"].value_or(std::string());
        if (catalog.product.empty() || catalog.module.empty() || catalog.ns.empty())
            problem(at(head), "the catalog names its product, the module holding the table and its "
                              "namespace");
    }
    std::set<std::string> names;
    if (const auto* list = root["operation"].as_array()) {
        for (const auto& node : *list) {
            const auto* entry = node.as_table();
            const int line = at(&node);
            if (!entry) {
                problem(line, "an [[operation]] is a table");
                continue;
            }
            Operation operation;
            operation.line = line;
            operation.name = (*entry)["name"].value_or(std::string());
            operation.effect = (*entry)["effect"].value_or(std::string());
            operation.summary = (*entry)["summary"].value_or(std::string());
            operation.result = (*entry)["result"].value_or(std::string());
            operation.cost = (*entry)["cost"].value_or(std::string("instant"));
            operation.undo = (*entry)["undo"].value_or(std::string());
            operation.person = (*entry)["person"].value_or(false);
            operation.cancellable = (*entry)["cancellable"].value_or(false);
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
            if (operation.effect == "read" && !operation.undo.empty())
                problem(line, std::format("`{}` reads: nothing to undo", operation.name));
            if (operation.effect != "read" && operation.undo.empty())
                problem(line, std::format("`{}` changes something: say how it is undone (undo; "
                                          "`none: why` when it cannot be)",
                                          operation.name));
            if (!operation.result.empty() && !type_name(operation.result))
                problem(line,
                        std::format("`{}`: result type `{}`", operation.name, operation.result));
            if (const auto* parameters = (*entry)["params"].as_array())
                for (const auto& item : *parameters) {
                    const auto* parameter = item.as_table();
                    if (!parameter) {
                        problem(at(&item), "a parameter is { name, type, doc }");
                        continue;
                    }
                    OpParameter value{(*parameter)["name"].value_or(std::string()),
                                      (*parameter)["type"].value_or(std::string()),
                                      (*parameter)["doc"].value_or(std::string()),
                                      (*parameter)["optional"].value_or(false)};
                    if (value.name.empty() || !type_name(value.type))
                        problem(at(&item),
                                std::format("`{}`: parameter `{}` needs a name and a type (bool, "
                                            "integer, number, seconds, string, json or a kind)",
                                            operation.name, value.name));
                    operation.parameters.push_back(std::move(value));
                }
            catalog.operations.push_back(std::move(operation));
        }
    }
    if (catalog.operations.empty())
        problem(1, "a catalog lists at least one [[operation]]");
    if (!problems.empty())
        return std::unexpected(std::move(problems));
    return catalog;
}

std::string emit_catalog_cpp(const Catalog& catalog, std::string_view source) {
    std::string out = std::format("// Generated by omui-gen ops from {} — do not edit.\n"
                                  "// {}'s operation catalog (convergence plan §10.3).\n"
                                  "export module {}:catalog;\n\nimport std;\nimport "
                                  "mlibs.control;\n\nexport namespace {} {{\n\n",
                                  source, catalog.product, catalog.module, catalog.ns);
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
    out += std::format("\n// {}\ninline constexpr std::array<mlibs::control::CatalogEntry, {}> "
                       "catalog{{{{\n",
                       catalog.doc, catalog.operations.size());
    for (const auto& operation : catalog.operations)
        out +=
            std::format("    {{{{{}, {}, {}}}, {}, {}, {}, mlibs::control::Cost::{}, {}, {}}},\n",
                        cpp_string(operation.name), effect_enum(operation.effect),
                        operation.person ? "true" : "false", cpp_string(operation.summary),
                        operation.parameters.empty() ? std::string("{}")
                                                     : "parameters_" + identifier(operation.name),
                        cpp_string(operation.result), operation.cost, cpp_string(operation.undo),
                        operation.cancellable ? "true" : "false");
    out += std::format("}}}};\n\n}}  // namespace {}\n", catalog.ns);
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
            const std::string schema = schema_type(parameter.type, parameter.doc);
            properties += std::format("{}{}: {}", properties.empty() ? "" : ", ",
                                      json_string(parameter.name), schema);
            if (!parameter.optional)
                required +=
                    std::format("{}{}", required.empty() ? "" : ", ", json_string(parameter.name));
        }
        out += std::format("    {{\"name\": {}, \"description\": {}, \"effect\": {}, \"person\": "
                           "{}, \"cost\": {}, \"undo\": {}, \"cancellable\": {}, "
                           "\"result\": {},\n     \"inputSchema\": {{\"type\": \"object\", "
                           "\"properties\": {{{}}}, \"required\": [{}]}}}}{}\n",
                           json_string(operation.name), json_string(operation.summary),
                           json_string(operation.effect), operation.person ? "true" : "false",
                           json_string(operation.cost), json_string(operation.undo),
                           operation.cancellable ? "true" : "false", json_string(operation.result),
                           properties, required, index + 1 < catalog.operations.size() ? "," : "");
    }
    return out + "  ]\n}\n";
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
                std::format("{}`{}`: {}{}", parameters.empty() ? "" : ", ", parameter.name,
                            parameter.type, parameter.optional ? " (optional)" : "");
        out +=
            std::format("| `{}` — {} | {}{} | {} | {} | {} | {} | {} |\n", operation.name,
                        operation.summary, operation.effect, operation.person ? " (a person)" : "",
                        parameters.empty() ? "—" : parameters,
                        operation.result.empty() ? "—" : operation.result, operation.cost,
                        operation.undo.empty() ? "—" : operation.undo,
                        operation.cancellable ? "yes" : "no");
    }
    return out;
}

}  // namespace omui::gen
