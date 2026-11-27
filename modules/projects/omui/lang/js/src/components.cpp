// The runtime's component catalogue (omuiCatalog's JSON) as the shapes omui.lang.ts writes TypeScript from: what the
// language's `describe` (an agent's design.api, every platform) and, off the Web, its `declare` (checker/) read.
module omui.lang.js;

import std;
import omui.lang;
import omui.lang.ts;

namespace omui::lang::js {

std::vector<ts::ComponentShape> components_of(const Json& catalog) {

    std::vector<ts::ComponentShape> out;
    if (!catalog.is_object() || !catalog.contains("components")) return out;
    for (auto it = catalog["components"].begin(); it != catalog["components"].end(); ++it) {
        const Json& c = it.value();
        ts::ComponentShape shape{.name = it.key(), .doc = c.value("doc", std::string()), .spec = c.value("spec", false)};
        if (c.contains("props"))
            for (auto p = c["props"].begin(); p != c["props"].end(); ++p) {
                ts::PropShape prop{.name = p.key(), .type = p.value().value("type", std::string("any")), .bindable = p.value().value("bindable", false),
                                   .doc = p.value().value("doc", std::string())};
                if (p.value().contains("values"))
                    for (const Json& v : p.value()["values"]) prop.values.push_back(v.get<std::string>());
                shape.props.push_back(std::move(prop));
            }
        if (c.contains("events"))
            for (auto e = c["events"].begin(); e != c["events"].end(); ++e) {
                ts::EventShape event{.name = e.key()};
                if (e.value().contains("args"))
                    for (auto a = e.value()["args"].begin(); a != e.value()["args"].end(); ++a)
                        event.args.push_back({.name = a.key(), .type = a.value().get<std::string>()});
                shape.events.push_back(std::move(event));
            }
        out.push_back(std::move(shape));
    }
    return out;
}

}  // namespace omui::lang::js
