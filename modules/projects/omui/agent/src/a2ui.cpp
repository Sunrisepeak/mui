// A2UI's messages read as view documents (omui.agent:a2ui).
module omui.agent;

import std;
import omui.core;

namespace omui::agent {

namespace {

// A data path (`/user/name`) as a signal's name (`user_name`).
std::string signal_name(std::string_view path) {
    std::string out;
    for (const char character : path) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) || character == '_') out += static_cast<char>(std::tolower(byte));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front()))) out.insert(0, "data_");
    return out;
}

// A string as a CEL literal.
std::string literal(std::string_view text) {
    std::string out = "'";
    for (const char character : text) {
        if (character == '\'' || character == '\\') out += '\\';
        out += character;
    }
    return out + "'";
}

std::string text_of(const Value& map, std::string_view key) {
    const Value* given = map.get(key);
    return given && given->type() == Value::Type::text ? std::string(given->as_string()) : std::string();
}

// dataModelUpdate's contents: {key, valueString | valueNumber | valueBoolean | valueMap}.
void read_contents(const std::string& prefix, const Value& contents, std::map<std::string, Value, std::less<>>& data) {
    if (!contents.list()) return;
    for (const Value& entry : *contents.list()) {
        const std::string path = prefix.empty() || prefix == "/" ? "/" + text_of(entry, "key") : prefix + "/" + text_of(entry, "key");
        if (const Value* text = entry.get("valueString")) data[signal_name(path)] = *text;
        else if (const Value* number = entry.get("valueNumber")) data[signal_name(path)] = Value(number->as_double());
        else if (const Value* truth = entry.get("valueBoolean")) data[signal_name(path)] = Value(truth->as_bool());
        else if (const Value* map = entry.get("valueMap")) read_contents(path, *map, data);
    }
}

// One surface's components, as a view document's nodes.
struct Builder {
    std::string_view                                  surface_id;
    const std::map<std::string, Value, std::less<>>&  components;
    const std::map<std::string, Value, std::less<>>&  data;
    std::vector<std::string>                          problems;
    Value                                             signals = Value(Value::Map{});
    std::set<std::string, std::less<>>                visiting;

    void problem(std::string_view id, std::string text) { problems.push_back(std::format("component `{}`: {}", id, std::move(text))); }

    void declare(const std::string& name, std::string_view kind) {
        if (signals.get(name)) return;
        const auto known = data.find(name);
        Value initial = known != data.end() ? known->second
                        : kind == "string"  ? Value(std::string())
                        : kind == "double"  ? Value(0.0)
                                            : Value(false);
        signals.set(name, Value{}.set("type", std::string(kind)).set("value", std::move(initial)));
    }

    // A bound value: a literal, or a data path (an expression, or a two-way binding).
    std::optional<Value> bound(std::string_view id, const Value* given, std::string_view kind, bool two_way) {
        if (!given) return std::nullopt;
        if (given->type() == Value::Type::text || given->is_number() || given->type() == Value::Type::boolean) return *given;
        if (const Value* text = given->get("literalString")) return *text;
        if (const Value* number = given->get("literalNumber")) return Value(number->as_double());
        if (const Value* truth = given->get("literalBoolean")) return Value(truth->as_bool());
        if (const Value* path = given->get("path")) {
            const std::string name = signal_name(path->as_string());
            declare(name, kind);
            return two_way ? Value{}.set("bind", name) : Value{}.set("expr", name);
        }
        problem(id, std::format("a value is a literal or a path, not {}", given->json()));
        return std::nullopt;
    }

    std::vector<std::string> children_of(std::string_view id, const Value& props) {
        std::vector<std::string> out;
        const Value* children = props.get("children");
        if (!children) return out;
        if (const Value* list = children->get("explicitList"); list && list->list())
            for (const Value& child : *list->list()) out.emplace_back(child.as_string());
        else if (children->get("template")) problem(id, "children from a template are not read yet: list them (explicitList)");
        return out;
    }

    std::optional<Value> node(const std::string& id) {
        const auto found = components.find(id);
        if (found == components.end()) {
            problems.push_back(std::format("component `{}` is named but was not sent", id));
            return std::nullopt;
        }
        if (!visiting.insert(id).second) {
            problem(id, "it holds itself");
            return std::nullopt;
        }
        struct Leave {
            std::set<std::string, std::less<>>& set;
            std::string                         id;
            ~Leave() { set.erase(id); }
        } leave{visiting, id};
        const Value& wrapper = found->second;
        if (!wrapper.map() || wrapper.map()->size() != 1) {
            problem(id, "its `component` is one type: {\"Text\": {…}}");
            return std::nullopt;
        }
        const auto& [kind, props] = *wrapper.map()->begin();
        Value out = Value{}.set("id", signal_name(id));
        Value element_props(Value::Map{});
        const auto with_children = [&](std::string_view type, Value layout) -> std::optional<Value> {
            Value::List children;
            for (const std::string& child : children_of(id, props))
                if (auto made = node(child)) children.push_back(std::move(*made));
            return out.set("type", std::string(type)).set("props", std::move(layout)).set("children", Value(std::move(children)));
        };
        const auto placed = [](const Value& props, Value layout) {
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 6> justify{{
                {"start", "start"}, {"center", "center"}, {"end", "end"},
                {"spaceBetween", "space_between"}, {"spaceAround", "space_around"}, {"spaceEvenly", "space_evenly"}}};
            for (const auto& [from, to] : justify)
                if (text_of(props, "distribution") == from) layout.set("justify", std::string(to));
            for (const std::string_view align : {"start", "center", "end", "stretch"})
                if (text_of(props, "alignment") == align) layout.set("align", std::string(align));
            return layout;
        };
        if (kind == "Text") {
            if (auto text = bound(id, props.get("text"), "string", false)) element_props.set("text", std::move(*text));
            const std::string hint = text_of(props, "usageHint");
            const std::string_view style = hint == "h1" ? "title" : hint == "h2" ? "heading" : hint == "caption" ? "caption" : hint.starts_with("h") ? "label" : "body";
            element_props.set("style", std::string(style)).set("origin", "remote");
            return out.set("type", "text").set("props", std::move(element_props));
        }
        if (kind == "Image") {
            if (auto url = bound(id, props.get("url"), "string", false)) element_props.set("source", std::move(*url));
            const std::string fit = text_of(props, "fit");
            if (!fit.empty()) element_props.set("fit", fit == "scaleDown" ? std::string("scale_down") : fit);
            return out.set("type", "image").set("props", std::move(element_props));
        }
        if (kind == "Row") return with_children("row", placed(props, Value{}.set("gap", 8)));
        if (kind == "Column") return with_children("column", placed(props, Value{}.set("gap", 8)));
        if (kind == "List") return with_children(text_of(props, "direction") == "horizontal" ? "row" : "column", placed(props, Value{}.set("gap", 8)));
        if (kind == "Card") {
            Value::List children;
            if (const Value* child = props.get("child"))
                if (auto made = node(std::string(child->as_string()))) children.push_back(std::move(*made));
            return out.set("type", "box")
                .set("props", Value{}.set("padding", 12).set("radius", 12).set("background", "surface").set("border_color", "border").set("border_width", 1))
                .set("children", Value(std::move(children)));
        }
        if (kind == "Button") {
            // Its label: the Text it holds.
            if (const Value* child = props.get("child")) {
                const auto label = components.find(child->as_string());
                if (label != components.end() && label->second.get("Text")) {
                    if (auto text = bound(child->as_string(), label->second.get("Text")->get("text"), "string", false)) element_props.set("text", std::move(*text));
                } else {
                    problem(id, "a button's label is a Text it holds (`child`)");
                }
            }
            if (const Value* primary = props.get("primary"); primary && primary->as_bool()) element_props.set("variant", "primary");
            Value args = Value{}.set("surfaceId", literal(surface_id)).set("sourceComponentId", literal(id));
            if (const Value* action = props.get("action")) {
                args.set("name", literal(text_of(*action, "name")));
                if (const Value* context = action->get("context"); context && context->list())
                    for (const Value& entry : *context->list()) {
                        const std::string key = text_of(entry, "key");
                        auto value = bound(id, entry.get("value"), "string", false);
                        if (key.empty() || !value) continue;
                        const Value* expression = value->get("expr");
                        args.set("context." + key, expression ? std::string(expression->as_string())
                                                              : value->type() == Value::Type::text ? literal(value->as_string()) : value->json());
                    }
            }
            Value::List press;
            press.push_back(Value{}.set("emit", "action").set("args", std::move(args)));
            return out.set("type", "button").set("props", std::move(element_props)).set("on", Value{}.set("activated", Value(std::move(press))));
        }
        if (kind == "CheckBox") {
            if (auto label = bound(id, props.get("label"), "string", false)) element_props.set("label", std::move(*label));
            if (auto value = bound(id, props.get("value"), "bool", true)) element_props.set("checked", std::move(*value));
            return out.set("type", "checkbox").set("props", std::move(element_props));
        }
        if (kind == "TextField") {
            if (auto label = bound(id, props.get("label"), "string", false)) element_props.set("label", std::move(*label));
            if (auto text = bound(id, props.get("text"), "string", true)) element_props.set("text", std::move(*text));
            const std::string field = text_of(props, "textFieldType");
            if (field == "longText") element_props.set("multiline", true);
            if (field == "obscured") element_props.set("password", true);
            return out.set("type", "text_box").set("props", std::move(element_props));
        }
        if (kind == "Slider") {
            if (auto value = bound(id, props.get("value"), "double", true)) element_props.set("value", std::move(*value));
            if (const Value* least = props.get("minValue")) element_props.set("min", least->as_double());
            if (const Value* most = props.get("maxValue")) element_props.set("max", most->as_double());
            return out.set("type", "slider").set("props", std::move(element_props));
        }
        if (kind == "Divider") {
            const bool across = text_of(props, "axis") == "vertical";
            return out.set("type", "box").set("props", across ? Value{}.set("width", 1).set("background", "border") : Value{}.set("height", 1).set("background", "border"));
        }
        problem(id, std::format("{} is not read yet (Text, Image, Row, Column, List, Card, Button, CheckBox, TextField, Slider and Divider are)", kind));
        return std::nullopt;
    }
};

}  // namespace

A2uiReader::Read A2uiReader::feed(std::string_view messages) {
    Read out;
    std::vector<Value> list;
    const auto whole = parse_json(messages);
    if (whole && whole->list()) list = *whole->list();
    else if (whole && whole->map()) list.push_back(*whole);
    else {
        std::size_t number = 0;
        for (const auto line : std::views::split(messages, '\n')) {
            ++number;
            const std::string_view text(line.begin(), line.end());
            if (text.find_first_not_of(" \t\r") == std::string_view::npos) continue;
            auto message = parse_json(text);
            if (!message) out.problems.push_back(std::format("line {}: not JSON: {}", number, message.error().text()));
            else list.push_back(std::move(*message));
        }
    }
    std::set<std::string> changed;
    for (std::size_t index = 0; index < list.size(); ++index) apply(list[index], index, changed, out);
    for (const std::string& name : changed) {
        const auto surface = surfaces_.find(name);
        if (surface == surfaces_.end() || !surface->second.rendering) continue;
        auto made = document(name, surface->second);
        if (made) out.rendered.push_back({name, std::move(*made)});
        else std::ranges::move(made.error(), std::back_inserter(out.problems));
    }
    return out;
}

void A2uiReader::apply(const Value& message, std::size_t index, std::set<std::string>& changed, Read& out) {
    const auto where = [&](std::string_view kind) { return std::format("message {} ({})", index + 1, kind); };
    if (const Value* update = message.get("surfaceUpdate")) {
        const std::string name = text_of(*update, "surfaceId");
        const Value* components = update->get("components");
        if (name.empty() || !components || !components->list()) {
            out.problems.push_back(std::format("{}: needs surfaceId and components", where("surfaceUpdate")));
            return;
        }
        Surface& surface = surfaces_[name];
        for (const Value& component : *components->list()) {
            const std::string id = text_of(component, "id");
            const Value* body = component.get("component");
            if (id.empty() || !body) {
                out.problems.push_back(std::format("{}: each component has an id and a component", where("surfaceUpdate")));
                continue;
            }
            surface.components.insert_or_assign(id, *body);
        }
        changed.insert(name);
    } else if (const Value* model = message.get("dataModelUpdate")) {
        const std::string name = text_of(*model, "surfaceId");
        if (name.empty()) {
            out.problems.push_back(std::format("{}: needs surfaceId", where("dataModelUpdate")));
            return;
        }
        const std::string path = text_of(*model, "path");
        if (const Value* contents = model->get("contents")) read_contents(path.empty() ? std::string("/") : path, *contents, surfaces_[name].data);
        changed.insert(name);
    } else if (const Value* begin = message.get("beginRendering")) {
        const std::string name = text_of(*begin, "surfaceId");
        if (name.empty() || text_of(*begin, "root").empty()) {
            out.problems.push_back(std::format("{}: needs surfaceId and root", where("beginRendering")));
            return;
        }
        Surface& surface = surfaces_[name];
        surface.root = text_of(*begin, "root");
        surface.rendering = true;
        changed.insert(name);
    } else if (const Value* gone = message.get("deleteSurface")) {
        const std::string name = text_of(*gone, "surfaceId");
        surfaces_.erase(name);
        changed.erase(name);
        out.deleted.push_back(name);
    } else {
        out.problems.push_back(std::format("message {}: not one A2UI reads (surfaceUpdate, dataModelUpdate, beginRendering, deleteSurface)", index + 1));
    }
}

std::expected<std::string, std::vector<std::string>> A2uiReader::document(std::string_view name, const Surface& surface) const {
    Builder builder{name, surface.components, surface.data};
    auto root = builder.node(surface.root);
    if (!root || !builder.problems.empty()) {
        if (builder.problems.empty()) builder.problems.push_back(std::format("surface `{}` has no root to render", name));
        for (std::string& problem : builder.problems) problem = std::format("surface `{}`: {}", name, problem);
        return std::unexpected(std::move(builder.problems));
    }
    Value view = Value{}.set("root", std::move(*root));
    if (builder.signals.map() && !builder.signals.map()->empty()) view.set("signals", std::move(builder.signals));
    return Value{}.set("omui", "0.1").set("view", std::move(view)).json();
}

}  // namespace omui::agent
