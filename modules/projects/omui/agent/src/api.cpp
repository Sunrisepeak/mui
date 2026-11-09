// The API an application gives what writes for it (omui.agent:api).
module omui.agent;

import std;
import omui.core;
import omui.view;
import omui.native;
import mlibs.control;

namespace omui::agent {

namespace {

// A catalog's operations as data: name, effect, summary, parameters (type,
// optional) — what an agent may do, beside what it may build with.
Value operations_of(std::span<const mlibs::control::CatalogEntry> catalog) {
    Value::List out;
    for (const auto& entry : catalog) {
        Value parameters(Value::Map{});
        for (const auto& parameter : entry.parameters) {
            Value one = Value{}.set("type", std::string(parameter.type)).set("doc", std::string(parameter.doc));
            if (parameter.optional) one.set("optional", true);
            parameters.set(std::string(parameter.name), std::move(one));
        }
        out.push_back(Value{}
                          .set("name", std::string(entry.operation.name))
                          .set("effect", std::string(spec::name(verb_of(entry).effect)))
                          .set("summary", std::string(entry.summary))
                          .set("params", std::move(parameters)));
    }
    return Value(std::move(out));
}

}  // namespace

Value api_of(Runtime& runtime) {
    Value out = component_catalog();
    auto natives = parse_json(native::catalog().dump());
    out.set("natives", natives ? natives->get("modules") ? *natives->get("modules") : Value(Value::Map{}) : Value(Value::Map{}));
    out.set("tokens", runtime.theme().describe());
    Value::List verbs;
    for (const VerbDesc* verb : runtime.verbs().list()) {
        Value args(Value::Map{});
        for (const auto& [name, type] : verb->args) args.set(name, type);
        Value one;
        one.set("name", verb->name).set("summary", verb->summary).set("effect", spec::name(verb->effect)).set("args", std::move(args));
        if (verb->person) one.set("person", true);
        verbs.push_back(std::move(one));
    }
    out.set("verbs", Value(std::move(verbs)));
    // What an agent may do with it: the protocol's operations, Live Design's and runtime generation's (the catalogs').
    out.set("operations", Value{}
                              .set("ui", operations_of(catalog))
                              .set("design", operations_of(design::catalog))
                              .set("generate", operations_of(generate::catalog)));
    return out;
}

VerbDesc verb_of(const mlibs::control::CatalogEntry& entry) {
    VerbDesc verb{.name = std::string(entry.operation.name),
                  .summary = std::string(entry.summary),
                  .person = entry.operation.person,
                  .undoable = entry.undoable};
    switch (entry.operation.effect) {
    case openas::Effect::read: verb.effect = Effect::read; break;
    case openas::Effect::reversible: verb.effect = Effect::reversible; break;
    case openas::Effect::irreversible: verb.effect = Effect::irreversible; break;
    default: verb.effect = Effect::external;
    }
    for (const auto& parameter : entry.parameters) verb.args.emplace_back(parameter.name, parameter.type);
    return verb;
}

}  // namespace omui::agent
