// The JSON Schema of view documents, and their canonical format.
module openmui.viewdoc;

import std;
import openmui.core;
import openmui.view;
import :document;
import :schema;

namespace omui::viewdoc {

namespace {

Value object() { return Value(Value::Map{}); }
Value list(std::initializer_list<Value> items) { return Value(Value::List(items)); }
Value refer(std::string_view def) { return object().set("$ref", std::format("#/$defs/{}", def)); }
Value typed(std::string_view type) { return object().set("type", type); }

Value strings(std::span<const std::string_view> names) {
    Value::List l;
    for (std::string_view n : names) l.emplace_back(n);
    return Value(std::move(l));
}

Value closed(Value properties, std::initializer_list<std::string_view> required = {}) {
    Value v = typed("object");
    v.set("properties", std::move(properties));
    if (required.size()) {
        Value::List r;
        for (std::string_view x : required) r.emplace_back(x);
        v.set("required", Value(std::move(r)));
    }
    v.set("additionalProperties", false);
    return v;
}

Value constant_of(const spec::PropInfo& p) {
    switch (p.type) {
        case spec::PropType::boolean: return typed("boolean");
        case spec::PropType::i32:
        case spec::PropType::i64: return typed("integer");
        case spec::PropType::f64: return typed("number");
        case spec::PropType::string: return typed("string");
        case spec::PropType::color: return typed("string").set("description", "a theme colour token or #rrggbb[aa]");
        case spec::PropType::choice: return object().set("enum", strings(p.values));
        case spec::PropType::string_list: return typed("array").set("items", typed("string"));
        case spec::PropType::any: return object();
    }
    return object();
}

std::string default_text(const spec::PropInfo& p) {
    using K = spec::DefaultValue::Kind;
    const spec::DefaultValue& d = p.default_value;
    switch (d.kind) {
        case K::boolean: return d.b ? "true" : "false";
        case K::integer: return std::to_string(d.i);
        case K::real: return std::format("{}", d.d);
        case K::text: return std::format("\"{}\"", d.s);
        case K::list: return "[]";
        case K::none: break;
    }
    return {};
}

Value component_schema(const ComponentType& t) {
    const spec::ContractInfo* c = t.contract;
    Value props = object();
    if (c)
        for (const spec::PropInfo& p : c->props) {
            Value s = object().set("anyOf", list({constant_of(p), refer(p.bindable ? "binding" : "oneway")}));
            std::string doc(p.doc);
            if (const std::string d = default_text(p); !d.empty()) doc += std::format("{}default {}", doc.empty() ? "" : " — ", d);
            if (p.bindable) doc += " (bindable)";
            if (!doc.empty()) s.set("description", doc);
            props.set(p.name, std::move(s));
        }
    // The runtime's own properties and events are named once, in $defs.
    Value props_schema = typed("object").set("properties", std::move(props));
    const bool closed_props = c || !t.set_prop;
    if (closed_props) {
        std::vector<std::string_view> own;
        if (c)
            for (const spec::PropInfo& p : c->props) own.push_back(p.name);
        props_schema.set("propertyNames", object().set("anyOf", list({object().set("enum", strings(own)), refer("runtime_property_name")})));
    }
    props_schema.set("additionalProperties", refer("runtime_property"));

    Value on = typed("object");
    if (c || !t.event) {
        std::vector<std::string_view> own;
        if (c)
            for (const spec::EventInfo& e : c->events) own.push_back(e.name);
        on.set("propertyNames", object().set("anyOf", list({object().set("enum", strings(own)), refer("runtime_event")})));
    }
    on.set("additionalProperties", refer("operations"));

    Value fields = object();
    fields.set("type", object().set("const", t.name));
    fields.set("id", refer("id"));
    fields.set("notes", typed("string"));
    fields.set("props", std::move(props_schema));
    fields.set("on", std::move(on));
    fields.set("animate", typed("object").set("additionalProperties", refer("transition")));
    fields.set("children", typed("array").set("items", refer("node")));
    fields.set("$comment", typed("string"));
    Value v = closed(std::move(fields), {"type"});
    if (c && !c->doc.empty()) v.set("description", c->doc);
    return v;
}

Value expression() {
    return typed("string").set("description", "A CEL expression (spec/registry/expr.toml): signals, models, properties, `event` in handlers");
}

Value defs(bool with_document_components) {
    Value d = object();
    d.set("id", typed("string").set("pattern", "^[A-Za-z0-9_.-]{1,128}$").set("description", "An explicit id (spec S-8): never display text"));
    d.set("expression", expression());
    d.set("runtime_property", object().set("description", "A property every node has (layout, look, motion, semantics)"));
    d.set("runtime_property_name", object().set("enum", strings(runtime_properties())));
    d.set("runtime_event", object().set("enum", strings(runtime_events())));

    Value expr_binding = closed(object().set("expr", refer("expression")), {"expr"});
    Value tr_binding = closed(object().set("tr", typed("string")).set("args", typed("object").set("additionalProperties", refer("expression"))), {"tr"});
    Value bind_binding = closed(object().set("bind", typed("string").set("description", "a signal, or a bindable interface property")), {"bind"});
    Value value_binding = closed(object().set("value", object()), {"value"});
    d.set("oneway", object().set("oneOf", list({expr_binding, tr_binding, value_binding})));
    d.set("binding", object().set("oneOf", list({expr_binding, tr_binding, bind_binding, value_binding})));

    Value args = typed("object").set("additionalProperties", object().set("description", "a string is an expression; anything else a constant"));
    Value set_op = closed(object().set("set", typed("string")).set("expr", refer("expression")).set("value", object()), {"set"});
    Value verb_op = closed(object().set("verb", typed("string").set("pattern", "^[a-z][a-z0-9_]*(\\.[a-z][a-z0-9_]*)*$")).set("args", args), {"verb"});
    Value emit_op = closed(object().set("emit", typed("string")).set("args", object().set("anyOf", list({args, typed("array").set("items", refer("expression"))}))),
                           {"emit"});
    d.set("operation", object().set("oneOf", list({set_op, verb_op, emit_op})));
    d.set("operations", typed("array").set("items", refer("operation")));

    Value spring = object().set("anyOf", list({typed("string"), closed(object()
                                                                            .set("stiffness", typed("number"))
                                                                            .set("damping", typed("number"))
                                                                            .set("mass", typed("number")))}));
    d.set("transition",
          object().set("oneOf", list({typed("string").set("description", "a motion token: motion.fast, motion.spring.snappy …"),
                                      closed(object().set("ease", typed("string")).set("ms", typed("number")).set("delay_ms", typed("number"))),
                                      closed(object().set("spring", spring).set("delay_ms", typed("number")), {"spring"})})));

    Value::List nodes;
    for (const ComponentType* t : registered_types()) {
        if (t->name == "root" || (t->create_in && !with_document_components)) continue;
        const std::string def = std::format("component.{}", t->name);
        d.set(def, component_schema(*t));
        nodes.push_back(refer(def));
    }
    d.set("if", closed(object()
                           .set("if", refer("expression"))
                           .set("then", typed("array").set("items", refer("node")))
                           .set("else", typed("array").set("items", refer("node")))
                           .set("notes", typed("string")),
                       {"if", "then"}));
    d.set("for", closed(object()
                            .set("for", typed("string").set("description", "the name of each row"))
                            .set("in", typed("string").set("description", "a model of the document"))
                            .set("where", refer("expression"))
                            .set("body", typed("array").set("items", refer("node")).set("minItems", 1).set("maxItems", 1))
                            .set("notes", typed("string")),
                        {"for", "in", "body"}));
    d.set("slot", closed(object().set("slot", typed("string")).set("notes", typed("string")), {"slot"}));
    nodes.push_back(refer("if"));
    nodes.push_back(refer("for"));
    nodes.push_back(refer("slot"));
    d.set("node", object().set("oneOf", Value(std::move(nodes))));

    Value signal_types = object().set("enum", list({"any", "bool", "double", "int", "list", "map", "string"}));
    d.set("signals", typed("object").set("additionalProperties",
                                         closed(object().set("type", signal_types).set("value", object()).set("doc", typed("string")))));
    d.set("models", typed("object").set("additionalProperties", object().set("anyOf", list({typed("string"), closed(object().set("type", typed("string")))}))));
    Value prop_types = object().set("enum", list({"any", "bool", "color", "enum", "f64", "i32", "i64", "list<string>", "string"}));
    d.set("interface",
          closed(object()
                     .set("role", typed("string"))
                     .set("doc", typed("string"))
                     .set("props", typed("object").set("additionalProperties", closed(object()
                                                                                          .set("type", prop_types)
                                                                                          .set("default", object())
                                                                                          .set("bindable", typed("boolean"))
                                                                                          .set("values", typed("array").set("items", typed("string")))
                                                                                          .set("doc", typed("string")),
                                                                                      {"type"})))
                     .set("events", typed("object").set("additionalProperties",
                                                        closed(object()
                                                                   .set("args", typed("object").set("additionalProperties", typed("string")))
                                                                   .set("doc", typed("string")))))));
    auto body = [](bool component) {
        Value f = object();
        f.set("name", component ? typed("string").set("pattern", "^[a-z][a-z0-9_]*$") : typed("string"));
        f.set("notes", typed("string"));
        if (component) f.set("interface", refer("interface"));
        f.set("signals", refer("signals"));
        f.set("models", refer("models"));
        f.set("root", refer("node"));
        return component ? closed(std::move(f), {"name", "root"}) : closed(std::move(f), {"root"});
    };
    d.set("view", body(false));
    d.set("component", body(true));
    return d;
}

// ── the canonical format ──

enum class Kind : std::uint8_t { document, body, node, props, binding, events, operation, signals, declaration, interface, other };

std::span<const std::string_view> order(Kind k) {
    static constexpr std::array<std::string_view, 5> document{"omui", "imports", "notes", "view", "component"};
    static constexpr std::array<std::string_view, 6> body{"name", "notes", "interface", "signals", "models", "root"};
    static constexpr std::array<std::string_view, 15> node{"type", "id", "if", "for", "in", "where", "slot", "notes", "props",
                                                           "on", "animate", "children", "then", "else", "body"};
    static constexpr std::array<std::string_view, 5> binding{"expr", "bind", "tr", "args", "value"};
    static constexpr std::array<std::string_view, 6> operation{"set", "verb", "emit", "expr", "value", "args"};
    static constexpr std::array<std::string_view, 6> declaration{"type", "value", "default", "bindable", "values", "doc"};
    static constexpr std::array<std::string_view, 4> interface{"role", "doc", "props", "events"};
    switch (k) {
        case Kind::document: return document;
        case Kind::body: return body;
        case Kind::node: return node;
        case Kind::binding: return binding;
        case Kind::operation: return operation;
        case Kind::declaration: return declaration;
        case Kind::interface: return interface;
        default: return {};
    }
}

// The kind of the value of `key` in an object of kind `k`.
Kind child(Kind k, std::string_view key) {
    switch (k) {
        case Kind::document: return key == "view" || key == "component" ? Kind::body : Kind::other;
        case Kind::body:
            if (key == "root") return Kind::node;
            if (key == "signals") return Kind::signals;
            if (key == "interface") return Kind::interface;
            return Kind::other;
        case Kind::node:
            if (key == "props") return Kind::props;
            if (key == "on") return Kind::events;
            if (key == "children" || key == "then" || key == "else" || key == "body") return Kind::node;  // lists of nodes
            return Kind::other;
        case Kind::props: return Kind::binding;
        case Kind::events: return Kind::operation;  // lists of operations
        case Kind::signals: return Kind::declaration;
        case Kind::interface: return key == "props" ? Kind::signals : key == "events" ? Kind::signals : Kind::other;
        default: return Kind::other;
    }
}

std::vector<std::pair<std::string_view, const Value*>> ordered(const Value::Map& m, Kind k) {
    std::vector<std::pair<std::string_view, const Value*>> out;
    for (std::string_view key : order(k))
        for (const auto& [name, v] : m)
            if (name == key) out.emplace_back(name, &v);
    for (const auto& [name, v] : m)
        if (std::ranges::find(order(k), name) == order(k).end()) out.emplace_back(name, &v);
    return out;
}

void compact(std::string& out, const Value& v, Kind k) {
    if (const Value::Map* m = v.map()) {
        out += '{';
        bool first = true;
        for (const auto& [key, x] : ordered(*m, k)) {
            if (!first) out += ", ";
            first = false;
            spec::append_json_string(out, key);
            out += ": ";
            compact(out, *x, child(k, key));
        }
        out += '}';
    } else if (const Value::List* l = v.list()) {
        out += '[';
        for (std::size_t i = 0; i < l->size(); ++i) {
            if (i) out += ", ";
            compact(out, (*l)[i], k);
        }
        out += ']';
    } else {
        out += v.json();
    }
}

bool has_nodes(const Value& v) {
    if (const Value::Map* m = v.map())
        for (std::string_view key : {"children", "then", "else", "body", "root"})
            for (const auto& [name, x] : *m)
                if (name == key && x.list() && !x.list()->empty()) return true;
    return false;
}

constexpr std::size_t width = 100;

void pretty(std::string& out, const Value& v, Kind k, std::size_t indent) {
    const bool container = v.map() || v.list();
    if (container && k != Kind::document && k != Kind::body && !has_nodes(v)) {
        std::string one;
        compact(one, v, k);
        if (indent + one.size() <= width && one.find('\n') == std::string::npos) {
            out += one;
            return;
        }
    }
    const std::string pad(indent + 2, ' ');
    if (const Value::Map* m = v.map()) {
        if (m->empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        const auto fields = ordered(*m, k);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            out += pad;
            spec::append_json_string(out, fields[i].first);
            out += ": ";
            pretty(out, *fields[i].second, child(k, fields[i].first), indent + 2);
            out += i + 1 < fields.size() ? ",\n" : "\n";
        }
        out += std::string(indent, ' ');
        out += '}';
    } else if (const Value::List* l = v.list()) {
        if (l->empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (std::size_t i = 0; i < l->size(); ++i) {
            out += pad;
            pretty(out, (*l)[i], k, indent + 2);
            out += i + 1 < l->size() ? ",\n" : "\n";
        }
        out += std::string(indent, ' ');
        out += ']';
    } else {
        out += v.json();
    }
}

}  // namespace

Value view_schema(bool with_document_components) {
    Value top = object();
    top.set("$schema", "https://json-schema.org/draft/2020-12/schema");
    top.set("$id", std::format("urn:openmui:view-document:{}", format_version));
    top.set("title", "openmui view document (*.view.omui.json)");
    top.set("description",
            "A view as data (view-data-files §3). Generated from the registered components by `omui schema`; "
            "`omui check` applies the same rules and more (names in expressions, ids, types of constants).");
    top.set("type", "object");
    top.set("properties", object()
                              .set("omui", typed("string").set("description", std::format("the format version, \"{}\"", format_version)))
                              .set("imports", typed("array").set("items", typed("string")))
                              .set("notes", typed("string"))
                              .set("view", refer("view"))
                              .set("component", refer("component"))
                              .set("$comment", typed("string")));
    top.set("required", list({"omui"}));
    top.set("oneOf", list({object().set("required", list({"view"})), object().set("required", list({"component"}))}));
    top.set("additionalProperties", false);
    top.set("$defs", defs(with_document_components));
    return top;
}

std::expected<std::string, Diagnostic> format_document(std::string_view json) {
    auto v = parse_json(json);
    if (!v) {
        Diagnostic d;
        d.code = "json";
        d.line = static_cast<std::uint32_t>(v.error().line);
        d.column = static_cast<std::uint32_t>(v.error().column);
        d.message = v.error().message;
        return std::unexpected(std::move(d));
    }
    std::string out;
    pretty(out, *v, Kind::document, 0);
    out += '\n';
    return out;
}

}  // namespace omui::viewdoc
