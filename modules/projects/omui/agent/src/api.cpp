// The API an application gives what writes for it (omui.agent:api).
module omui.agent;

import std;
import omui.core;
import omui.view;
import omui.native;

namespace omui::agent {

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
    return out;
}

}  // namespace omui::agent
