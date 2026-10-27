// Reading and checking a view document.
module openmui.viewdoc;

import std;
import openmui.core;
import openmui.view;
import openmui.expr;
import :document;

namespace omui::viewdoc {

namespace {

using Level = Diagnostic::Level;

constexpr std::array<std::string_view, 7> signal_types{"any", "bool", "double", "int", "list", "map", "string"};
constexpr std::array<std::string_view, 9> prop_types{"any", "bool", "color", "enum", "f64", "i32", "i64", "list<string>", "string"};
// CEL's reserved words and the names the document gives itself.
constexpr std::array<std::string_view, 8> reserved{"event", "false", "has", "in", "null", "true", "select", "omui"};

std::size_t distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    std::iota(row.begin(), row.end(), std::size_t{0});
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diag = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t up = row[j];
            const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (same ? 0 : 1)});
            diag = up;
        }
    }
    return row[b.size()];
}

template <class Names>
std::string nearest(std::string_view name, const Names& names) {
    std::string best;
    std::size_t d = std::max<std::size_t>(2, name.size() / 3) + 1;
    for (const auto& n : names) {
        const std::string_view s = n;
        if (const std::size_t x = distance(name, s); x < d) {
            d = x;
            best = std::string(s);
        }
    }
    return best.empty() ? std::string() : std::format("did you mean `{}`?", best);
}

// "TextBox" and "text-box" are written text_box.
std::string snake(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0 && s[i - 1] != '_' && s[i - 1] != '-') out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c == '-' ? '_' : c;
        }
    }
    return out;
}

bool identifier(std::string_view s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::ranges::all_of(s, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

std::string escape_token(std::string_view key) {
    std::string out;
    for (char c : key) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

std::string at(std::string_view pointer, std::string_view key) { return std::format("{}/{}", pointer, escape_token(key)); }
std::string at(std::string_view pointer, std::size_t index) { return std::format("{}/{}", pointer, index); }

// Every function a call names, for a check before it runs.
void calls(const expr::Node& n, std::vector<std::pair<std::string, std::uint32_t>>& out) {
    if (n.op == expr::Op::call) out.emplace_back(n.name, n.at);
    for (const auto& a : n.args) calls(*a, out);
}

// Whether a constant fits a contract property's type.
bool fits(const spec::PropInfo& p, const Value& v) {
    using T = Value::Type;
    switch (p.type) {
        case spec::PropType::boolean: return v.type() == T::boolean;
        case spec::PropType::i32:
        case spec::PropType::i64: return v.type() == T::integer || (v.type() == T::real && v.as_double() == std::floor(v.as_double()));
        case spec::PropType::f64: return v.is_number();
        case spec::PropType::string: return v.type() == T::text;
        case spec::PropType::color: return v.type() == T::text;
        case spec::PropType::choice:
            return v.type() == T::text && (p.values.empty() || std::ranges::find(p.values, v.as_string()) != p.values.end());
        case spec::PropType::string_list:
            return v.list() && std::ranges::all_of(*v.list(), [](const Value& x) { return x.type() == T::text; });
        case spec::PropType::any: return true;
    }
    return true;
}

std::string choices(const spec::PropInfo& p) {
    std::string out;
    for (std::string_view v : p.values) out += std::format("{}`{}`", out.empty() ? "" : ", ", v);
    return out;
}

struct Scope {
    std::vector<std::string> names;  // in scope, innermost last
    [[nodiscard]] bool has(std::string_view n) const { return std::ranges::find(names, n) != names.end(); }
};

class Reader {
public:
    Reader(std::string_view text, const ReadOptions& o) : text_(text), o_(o) {}

    ReadResult run() {
        ReadResult out;
        auto json = parse_json(text_);
        if (!json) {
            Diagnostic d;
            d.code = "json";
            d.line = static_cast<std::uint32_t>(json.error().line);
            d.column = static_cast<std::uint32_t>(json.error().column);
            d.message = json.error().message;
            out.diagnostics.push_back(std::move(d));
            return out;
        }
        document(*json);
        for (Diagnostic& d : diags_)
            if (d.line == 0)
                if (auto place = json_location(text_, d.pointer)) {
                    d.line = static_cast<std::uint32_t>(place->line);
                    d.column = static_cast<std::uint32_t>(place->column);
                }
        std::ranges::stable_sort(diags_, [](const Diagnostic& a, const Diagnostic& b) {
            return std::tie(a.line, a.column) < std::tie(b.line, b.column);
        });
        out.diagnostics = std::move(diags_);
        if (out.errors() == 0) out.document = std::move(doc_);
        return out;
    }

private:
    void report(Level level, std::string pointer, std::string code, std::string message, std::string hint = {}) {
        Diagnostic d;
        d.level = level;
        d.pointer = std::move(pointer);
        d.code = std::move(code);
        d.message = std::move(message);
        d.hint = std::move(hint);
        diags_.push_back(std::move(d));
    }
    void error(std::string pointer, std::string code, std::string message, std::string hint = {}) {
        report(Level::error, std::move(pointer), std::move(code), std::move(message), std::move(hint));
    }
    void warning(std::string pointer, std::string code, std::string message, std::string hint = {}) {
        report(Level::warning, std::move(pointer), std::move(code), std::move(message), std::move(hint));
    }

    // Fields a part of the format does not define: skipped, with a warning.
    void unknown_fields(const Value& v, std::string_view pointer, std::span<const std::string_view> known) {
        if (!v.map()) return;
        for (const auto& [k, x] : *v.map())
            if (std::ranges::find(known, k) == known.end() && k != "$comment")
                warning(at(pointer, k), "unknown-field", std::format("`{}` is not a field here; it is skipped", k), nearest(k, known));
    }

    const ComponentType* type_named(std::string_view name) const { return o_.types ? o_.types(name) : find_type(name); }

    // ── the document ──

    void document(const Value& v) {
        if (!v.map()) {
            error("", "document", "a view document is a JSON object", R"(start with {"omui": "0.1", "view": {…}})");
            return;
        }
        unknown_fields(v, "", std::array<std::string_view, 5>{"omui", "imports", "view", "component", "notes"});
        const Value* version = v.get("omui");
        if (!version || version->type() != Value::Type::text) {
            error("", "version", "the document states no format version", std::format(R"(add "omui": "{}")", format_version));
        } else {
            doc_.version = std::string(version->as_string());
            version_check(doc_.version);
        }
        if (const Value* imports = v.get("imports")) {
            if (!imports->list()) error("/imports", "imports", "`imports` is a list of component packages");
            else
                for (std::size_t i = 0; i < imports->list()->size(); ++i) {
                    const Value& p = (*imports->list())[i];
                    if (p.type() != Value::Type::text || p.as_string().empty())
                        error(at("/imports", i), "imports", "an import is a package name", R"("openmui.ui.base")");
                    else doc_.imports.emplace_back(p.as_string());
                }
        }
        if (const Value* notes = v.get("notes"); notes && notes->type() == Value::Type::text) doc_.notes = std::string(notes->as_string());
        const Value* view = v.get("view");
        const Value* component = v.get("component");
        if (view && component) {
            error("/component", "document", "a document is a view or a component, not both");
            return;
        }
        if (!view && !component) {
            error("", "document", "the document has no `view` or `component`", R"(add "view": {"root": {…}})");
            return;
        }
        doc_.component = component != nullptr;
        body(doc_.component ? *component : *view, doc_.component ? "/component" : "/view");
    }

    void version_check(std::string_view v) {
        unsigned major = 0, minor = 0;
        const auto dot = v.find('.');
        const bool parsed = dot != std::string_view::npos &&
                            std::from_chars(v.data(), v.data() + dot, major).ptr == v.data() + dot &&
                            std::from_chars(v.data() + dot + 1, v.data() + v.size(), minor).ptr == v.data() + v.size();
        if (!parsed) {
            error("/omui", "version", std::format("`{}` is not a format version", v), std::format("write \"{}\"", format_version));
            return;
        }
        if (major != 0) error("/omui", "version", std::format("format {} is not one this reader knows ({})", v, format_version));
        else if (minor > 1) warning("/omui", "version", std::format("format {} is newer than this reader's {}: what is new is skipped", v, format_version));
    }

    void body(const Value& v, const std::string& p) {
        if (!v.map()) {
            error(p, "document", std::format("`{}` is an object", p.substr(1)));
            return;
        }
        unknown_fields(v, p, std::array<std::string_view, 6>{"name", "notes", "interface", "signals", "models", "root"});
        if (const Value* name = v.get("name")) {
            if (name->type() != Value::Type::text) error(at(p, "name"), "name", "`name` is a string");
            else doc_.name = std::string(name->as_string());
        }
        if (doc_.component) {
            static constexpr auto lower = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
            if (doc_.name.empty()) error(p, "name", "a component document has a `name`: its type", R"("name": "search_box")");
            else if (!(doc_.name[0] >= 'a' && doc_.name[0] <= 'z') || !std::ranges::all_of(doc_.name, lower))
                error(at(p, "name"), "name", std::format("`{}`: a component's name is lower case words joined by _", doc_.name),
                      std::format("\"{}\"", snake(doc_.name)));
            if (const Value* iface = v.get("interface")) interface(*iface, at(p, "interface"));
        } else if (v.get("interface")) {
            error(at(p, "interface"), "interface", "only a component document has an interface", R"(write "component" rather than "view")");
        }
        if (const Value* s = v.get("signals")) signals(*s, at(p, "signals"));
        if (const Value* m = v.get("models")) models(*m, at(p, "models"));
        const Value* root = v.get("root");
        if (!root) {
            error(p, "root", "the view has no `root` node", R"(add "root": {"type": "column", "children": […]})");
            return;
        }
        Scope scope;
        for (const InterfaceProp& x : doc_.interface.props) scope.names.push_back(x.name);
        for (const SignalDecl& x : doc_.signals) scope.names.push_back(x.name);
        for (const auto& [name, type] : doc_.models) scope.names.push_back(name);
        doc_.root = node(*root, at(p, "root"), scope, false);
    }

    void name_check(std::string_view name, const std::string& p, std::string_view what) {
        if (!identifier(name)) error(p, "name", std::format("`{}` is not a name {} can have", name, what), "letters, digits and _, not starting with a digit");
        else if (std::ranges::find(reserved, name) != reserved.end())
            error(p, "name", std::format("`{}` is reserved", name));
        else if (taken_.contains(std::string(name)))
            error(p, "name", std::format("`{}` names two things", name), "signals, models and interface properties share one set of names");
        taken_.insert(std::string(name));
    }

    void interface(const Value& v, const std::string& p) {
        if (!v.map()) {
            error(p, "interface", "`interface` is an object");
            return;
        }
        unknown_fields(v, p, std::array<std::string_view, 4>{"props", "events", "role", "doc"});
        if (const Value* role = v.get("role")) {
            auto r = role->type() == Value::Type::text ? spec::parse_role(role->as_string()) : std::nullopt;
            if (!r) error(at(p, "role"), "role", "`role` is a role of the registry", R"("group", "toolbar", "form" …)");
            else doc_.interface.role = *r;
        }
        if (const Value* d = v.get("doc"); d && d->type() == Value::Type::text) doc_.interface.doc = std::string(d->as_string());
        if (const Value* props = v.get("props")) {
            if (!props->map()) error(at(p, "props"), "interface", "`props` is an object of properties");
            else
                for (const auto& [name, decl] : *props->map()) {
                    const std::string q = at(at(p, "props"), name);
                    name_check(name, q, "a property");
                    InterfaceProp x;
                    x.name = name;
                    if (!decl.map()) {
                        error(q, "interface", "a property is declared by an object", R"({"type": "string", "default": ""})");
                        continue;
                    }
                    unknown_fields(decl, q, std::array<std::string_view, 5>{"type", "default", "bindable", "values", "doc"});
                    const Value* type = decl.get("type");
                    x.type = type && type->type() == Value::Type::text ? std::string(type->as_string()) : "";
                    if (std::ranges::find(prop_types, x.type) == prop_types.end())
                        error(at(q, "type"), "type", std::format("`{}` is not a property type", x.type), nearest(x.type, prop_types));
                    if (const Value* values = decl.get("values"); values && values->list())
                        for (const Value& e : *values->list()) x.values.emplace_back(e.as_string());
                    if (x.type == "enum" && x.values.empty()) error(q, "type", "an enum lists its `values`");
                    if (const Value* b = decl.get("bindable")) x.bindable = b->as_bool();
                    if (const Value* d = decl.get("doc"); d && d->type() == Value::Type::text) x.doc = std::string(d->as_string());
                    if (const Value* def = decl.get("default")) x.default_value = *def;
                    doc_.interface.props.push_back(std::move(x));
                }
        }
        if (const Value* events = v.get("events")) {
            if (!events->map()) error(at(p, "events"), "interface", "`events` is an object of events");
            else
                for (const auto& [name, decl] : *events->map()) {
                    const std::string q = at(at(p, "events"), name);
                    if (!identifier(name)) error(q, "name", std::format("`{}` is not an event name", name));
                    InterfaceEvent e;
                    e.name = name;
                    if (const Value* args = decl.get("args"); args && args->map())
                        for (const auto& [a, t] : *args->map()) e.args.emplace_back(a, std::string(t.as_string()));
                    if (const Value* d = decl.get("doc"); d && d->type() == Value::Type::text) e.doc = std::string(d->as_string());
                    doc_.interface.events.push_back(std::move(e));
                }
        }
    }

    void signals(const Value& v, const std::string& p) {
        if (!v.map()) {
            error(p, "signals", "`signals` is an object: name → {type, value}");
            return;
        }
        for (const auto& [name, decl] : *v.map()) {
            const std::string q = at(p, name);
            name_check(name, q, "a signal");
            SignalDecl s;
            s.name = name;
            s.pointer = q;
            if (!decl.map()) {
                error(q, "signals", "a signal is declared by an object", R"({"type": "int", "value": 0})");
                continue;
            }
            unknown_fields(decl, q, std::array<std::string_view, 4>{"type", "value", "provided", "doc"});
            if (const Value* p = decl.get("provided")) {
                if (p->type() != Value::Type::boolean) error(at(q, "provided"), "signals", "`provided` is true or false");
                else s.provided = p->as_bool();
                if (s.provided && doc_.component)
                    error(at(q, "provided"), "signals", "a component's state is its own: it takes values through its interface",
                          "declare an interface property instead");
            }
            const Value* type = decl.get("type");
            s.type = type && type->type() == Value::Type::text ? std::string(type->as_string()) : "any";
            if (std::ranges::find(signal_types, s.type) == signal_types.end()) {
                error(at(q, "type"), "type", std::format("`{}` is not a signal type", s.type), nearest(s.type, signal_types));
                s.type = "any";
            }
            if (const Value* value = decl.get("value"); value && s.provided) {
                error(at(q, "value"), "signals", std::format("`{}` is provided by the host, which gives its value", name),
                      "remove the value, or the `provided`");
            } else if (value) {
                auto c = coerce(s.type, *value);
                if (!c) error(at(q, "value"), "type", std::format("the value of `{}` is not a {}", name, s.type));
                else s.value = std::move(*c);
            } else {
                s.value = s.type == "bool"     ? Value(false)
                          : s.type == "int"    ? Value(0)
                          : s.type == "double" ? Value(0.0)
                          : s.type == "string" ? Value("")
                          : s.type == "list"   ? Value(Value::List{})
                          : s.type == "map"    ? Value(Value::Map{})
                                               : Value{};
            }
            doc_.signals.push_back(std::move(s));
        }
    }

    void models(const Value& v, const std::string& p) {
        if (!v.map()) {
            error(p, "models", "`models` is an object: name → the type of its rows");
            return;
        }
        for (const auto& [name, decl] : *v.map()) {
            const std::string q = at(p, name);
            name_check(name, q, "a model");
            std::string type = decl.type() == Value::Type::text ? std::string(decl.as_string())
                               : decl.map() && decl.get("type")  ? std::string(decl.get("type")->as_string())
                                                                 : "row";
            doc_.models.emplace_back(name, std::move(type));
        }
    }

    // ── expressions ──

    // The text offset of a byte of a JSON string's decoded content, so an
    // expression's diagnostic points into the document.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> place_in_string(const std::string& pointer, std::uint32_t decoded) const {
        auto start = json_location(text_, pointer);
        if (!start || start->offset >= text_.size() || text_[start->offset] != '"') return std::nullopt;
        std::size_t i = start->offset + 1;
        std::uint32_t seen = 0;
        std::uint32_t column = static_cast<std::uint32_t>(start->column) + 1;
        while (i < text_.size() && text_[i] != '"' && seen < decoded) {
            if (text_[i] == '\\') {
                const char e = i + 1 < text_.size() ? text_[i + 1] : ' ';
                const std::size_t len = e == 'u' ? 6 : 2;
                seen += e == 'u' ? 1 : 1;  // an escape is one byte or code point of the content: near enough
                i += len;
                column += static_cast<std::uint32_t>(len);
                continue;
            }
            if ((static_cast<unsigned char>(text_[i]) & 0xC0) != 0x80) ++column;
            ++seen;
            ++i;
        }
        return std::pair{static_cast<std::uint32_t>(start->line), column};
    }

    std::optional<expr::Expr> expression(const Value& v, const std::string& p, const Scope& scope) {
        if (v.type() != Value::Type::text) {
            error(p, "expression", "an expression is a string of CEL", R"("count + 1")");
            return std::nullopt;
        }
        auto e = expr::parse(v.as_string());
        if (!e) {
            Diagnostic d;
            d.pointer = p;
            d.code = "expression";
            d.message = e.error().message;
            d.hint = e.error().hint;
            if (auto place = place_in_string(p, e.error().at)) std::tie(d.line, d.column) = *place;
            diags_.push_back(std::move(d));
            return std::nullopt;
        }
        for (const std::string& name : e->references())
            if (!scope.has(name)) {
                std::string hint = nearest(name, scope.names);
                if (hint.empty()) hint = R"(declare it in "signals" (or "models"))";
                error(p, "unknown-name", std::format("`{}` is not a signal, model or property of this document", name), hint);
            }
        std::vector<std::pair<std::string, std::uint32_t>> named;
        calls(e->node(), named);
        const auto known = expr::functions();
        for (const auto& [f, offset] : named)
            if (std::ranges::find(known, f) == known.end()) {
                Diagnostic d;
                d.pointer = p;
                d.code = "unknown-function";
                d.message = std::format("no function `{}`", f);
                d.hint = nearest(f, known);
                if (auto place = place_in_string(p, offset)) std::tie(d.line, d.column) = *place;
                diags_.push_back(std::move(d));
            }
        return std::move(*e);
    }

    // ── nodes ──

    Binding binding(const Value& v, const std::string& p, const Scope& scope, const spec::PropInfo* info, std::string_view prop) {
        Binding b;
        b.pointer = p;
        const Value::Map* m = v.map();
        auto only = [&](std::initializer_list<std::string_view> keys) {
            return m && !m->empty() && std::ranges::all_of(*m, [&](const auto& e) { return std::ranges::find(keys, e.first) != keys.end(); });
        };
        if (only({"expr"})) {
            b.kind = Binding::Kind::expr;
            if (auto e = expression(*v.get("expr"), at(p, "expr"), scope)) b.expr = std::move(*e);
            return b;
        }
        if (only({"bind"})) {
            b.kind = Binding::Kind::bind;
            const Value& name = *v.get("bind");
            b.name = name.type() == Value::Type::text ? std::string(name.as_string()) : "";
            const bool signal = doc_.signal(b.name) != nullptr;
            const InterfaceProp* iface = doc_.interface.prop(b.name);
            if (!signal && !iface)
                error(at(p, "bind"), "unknown-signal", std::format("`{}` is not a signal of this document", b.name), nearest(b.name, scope.names));
            else if (iface && !iface->bindable)
                error(at(p, "bind"), "not-bindable", std::format("the interface property `{}` is not bindable", b.name),
                      R"(declare it "bindable": true, or read it with {"expr": …})");
            if (info && !info->bindable)
                error(at(p, "bind"), "not-bindable", std::format("`{}` is not a bindable property", prop),
                      std::format(R"(show the signal one way: {{"expr": "{}"}})", b.name));
            return b;
        }
        if (only({"tr", "args"}) && v.get("tr")) {
            b.kind = Binding::Kind::tr;
            const Value& key = *v.get("tr");
            if (key.type() != Value::Type::text || key.as_string().empty()) {
                error(at(p, "tr"), "tr", "`tr` is a message key");
                return b;
            }
            std::vector<expr::Expr> args{expr::Expr(key.as_string())};
            if (const Value* a = v.get("args")) {
                if (!a->list()) error(at(p, "args"), "tr", "`args` is a list of expressions");
                else
                    for (std::size_t i = 0; i < a->list()->size(); ++i)
                        if (auto e = expression((*a->list())[i], at(at(p, "args"), i), scope)) args.push_back(std::move(*e));
            }
            b.expr = expr::call("tr", std::move(args));
            return b;
        }
        if (only({"value"})) {
            b.value = *v.get("value");  // a constant that could read as a binding
        } else {
            b.value = v;
        }
        if (info && !fits(*info, b.value)) {
            std::string hint = info->type == spec::PropType::choice ? std::format("one of {}", choices(*info)) : std::string();
            error(p, "type", std::format("`{}` is a {}; {} is not", prop, spec::name(info->type), b.value.json()), hint);
        }
        return b;
    }

    std::vector<Op> ops(const Value& v, const std::string& p, Scope scope, bool interface_events) {
        std::vector<Op> out;
        if (!v.list()) {
            error(p, "event", "an event's operations are a list", R"([{"set": "count", "expr": "count + 1"}])");
            return out;
        }
        scope.names.push_back("event");
        for (std::size_t i = 0; i < v.list()->size(); ++i) {
            const Value& o = (*v.list())[i];
            const std::string q = at(p, i);
            if (!o.map()) {
                error(q, "operation", "an operation is an object: set, verb or emit");
                continue;
            }
            const int kinds = (o.get("set") ? 1 : 0) + (o.get("verb") ? 1 : 0) + (o.get("emit") ? 1 : 0);
            if (kinds != 1) {
                error(q, "operation", "an operation is exactly one of set, verb and emit",
                      "a view document's events only set signals, invoke verbs and emit events (R4)");
                continue;
            }
            Op op;
            op.pointer = q;
            if (const Value* set = o.get("set")) {
                op.kind = Op::Kind::set;
                unknown_fields(o, q, std::array<std::string_view, 3>{"set", "expr", "value"});
                op.target = set->type() == Value::Type::text ? std::string(set->as_string()) : "";
                const InterfaceProp* iface = doc_.interface.prop(op.target);
                if (!doc_.signal(op.target) && !(iface && iface->bindable))
                    error(at(q, "set"), "unknown-signal", std::format("`{}` is not a signal of this document", op.target),
                          iface ? std::string("an interface property is set from outside, unless it is bindable") : nearest(op.target, scope.names));
                const Value* e = o.get("expr");
                const Value* value = o.get("value");
                if ((e != nullptr) == (value != nullptr)) {
                    error(q, "operation", "`set` takes an `expr` or a `value`", R"({"set": "count", "expr": "count + 1"})");
                } else if (e) {
                    if (auto x = expression(*e, at(q, "expr"), scope)) op.value = std::move(*x);
                } else {
                    op.value = expr::lit(*value);
                    if (const SignalDecl* s = doc_.signal(op.target); s && !coerce(s->type, *value))
                        error(at(q, "value"), "type", std::format("`{}` is a {}; {} is not", op.target, s->type, value->json()));
                }
            } else if (const Value* verb = o.get("verb")) {
                op.kind = Op::Kind::verb;
                unknown_fields(o, q, std::array<std::string_view, 2>{"verb", "args"});
                op.target = verb->type() == Value::Type::text ? std::string(verb->as_string()) : "";
                if (!valid_verb_name(op.target))
                    error(at(q, "verb"), "verb", std::format("`{}` is not a verb name", op.target), "lower-case words joined by dots: note.delete");
                else if (o_.verbs && !o_.verbs->find(op.target))
                    warning(at(q, "verb"), "unknown-verb", std::format("no verb `{}` is registered", op.target));
                args(o, q, scope, op);
            } else {
                op.kind = Op::Kind::emit;
                unknown_fields(o, q, std::array<std::string_view, 2>{"emit", "args"});
                op.target = o.get("emit")->type() == Value::Type::text ? std::string(o.get("emit")->as_string()) : "";
                const InterfaceEvent* declared = doc_.interface.event(op.target);
                if (interface_events && !declared)
                    error(at(q, "emit"), "unknown-event", std::format("`{}` is not an event of the interface", op.target),
                          nearest(op.target, doc_.interface.events | std::views::transform(&InterfaceEvent::name)));
                if (const Value* a = o.get("args"); a && a->list()) {
                    // Positional: the declared arguments, in order.
                    const std::size_t n = a->list()->size();
                    if (declared && n != declared->args.size())
                        error(at(q, "args"), "arguments", std::format("`{}` has {} argument{}; {} given", op.target, declared->args.size(),
                                                                      declared->args.size() == 1 ? "" : "s", n));
                    for (std::size_t k = 0; k < n; ++k) {
                        std::string name = declared && k < declared->args.size() ? declared->args[k].first : std::format("{}", k);
                        if (auto x = expression((*a->list())[k], at(at(q, "args"), k), scope)) op.args.emplace_back(std::move(name), std::move(*x));
                    }
                } else {
                    args(o, q, scope, op);
                    if (declared)
                        for (const auto& [name, x] : op.args)
                            if (std::ranges::find(declared->args, name, &std::pair<std::string, std::string>::first) == declared->args.end())
                                error(at(at(q, "args"), name), "arguments", std::format("`{}` has no argument `{}`", op.target, name));
                }
            }
            out.push_back(std::move(op));
        }
        return out;
    }

    // Named arguments: a string is an expression, anything else a constant.
    void args(const Value& o, const std::string& q, const Scope& scope, Op& op) {
        const Value* a = o.get("args");
        if (!a) return;
        if (!a->map()) {
            error(at(q, "args"), "arguments", "`args` is an object: name → expression");
            return;
        }
        for (const auto& [name, x] : *a->map()) {
            if (x.type() == Value::Type::text) {
                if (auto e = expression(x, at(at(q, "args"), name), scope)) op.args.emplace_back(name, std::move(*e));
            } else {
                op.args.emplace_back(name, expr::lit(x));
            }
        }
    }

    std::optional<Transition> transition(const Value& v, const std::string& p) {
        if (v.type() == Value::Type::text) {
            if (auto t = Transition::token(v.as_string())) return t;
            error(p, "transition", std::format("`{}` is not a motion token", v.as_string()), R"("motion.fast", "motion.spring.snappy" …)");
            return std::nullopt;
        }
        if (!v.map()) {
            error(p, "transition", "a transition is a motion token, {\"ease\", \"ms\"} or {\"spring\"}");
            return std::nullopt;
        }
        unknown_fields(v, p, std::array<std::string_view, 4>{"ease", "ms", "delay_ms", "spring"});
        Transition t;
        if (const Value* d = v.get("delay_ms")) t.delay = anim::seconds(d->as_double() / 1000.0);
        if (const Value* s = v.get("spring")) {
            t.kind = Transition::Kind::spring;
            if (s->type() == Value::Type::text) {
                const std::string token = std::format("motion.spring.{}", s->as_string());
                if (auto x = Transition::token(token)) t.spring = x->spring;
                else error(at(p, "spring"), "transition", std::format("no spring is named `{}`", s->as_string()), "gentle, snappy, wobbly …");
            } else if (s->map()) {
                if (const Value* k = s->get("stiffness")) t.spring.stiffness = k->as_double();
                if (const Value* c = s->get("damping")) t.spring.damping = c->as_double();
                if (const Value* m = s->get("mass")) t.spring.mass = m->as_double();
                if (!(t.spring.stiffness > 0) || !(t.spring.mass > 0) || t.spring.damping < 0)
                    error(at(p, "spring"), "transition", "a spring needs stiffness > 0, mass > 0, damping ≥ 0");
            }
            return t;
        }
        t.kind = Transition::Kind::tween;
        if (const Value* ms = v.get("ms")) {
            if (!ms->is_number() || ms->as_double() < 0) error(at(p, "ms"), "transition", "`ms` is a duration in milliseconds");
            else t.duration = anim::seconds(ms->as_double() / 1000.0);
        }
        if (const Value* e = v.get("ease")) {
            auto ease = e->type() == Value::Type::text ? anim::parse_ease(e->as_string()) : std::nullopt;
            if (!ease) error(at(p, "ease"), "transition", "`ease` names a curve", "out_cubic, in_out_sine, cubic-bezier(…) …");
            else t.ease = *ease;
        }
        return t;
    }

    Node node(const Value& v, const std::string& p, const Scope& scope, bool row) {
        Node n;
        n.pointer = p;
        if (!v.map()) {
            error(p, "node", "a node is an object", R"({"type": "text", "props": {"text": "Hello"}})");
            return n;
        }
        const int kinds = (v.get("type") ? 1 : 0) + (v.get("if") ? 1 : 0) + (v.get("for") ? 1 : 0) + (v.get("slot") ? 1 : 0);
        if (kinds != 1) {
            error(p, "node", kinds == 0 ? "a node has a `type`, an `if`, a `for` or a `slot`" : "a node is one of type, if, for and slot");
            return n;
        }
        if (v.get("if")) return when(v, p, scope);
        if (v.get("for")) return each(v, p, scope);
        if (const Value* slot = v.get("slot")) {
            n.kind = Node::Kind::slot;
            unknown_fields(v, p, std::array<std::string_view, 2>{"slot", "notes"});
            n.slot = slot->type() == Value::Type::text ? std::string(slot->as_string()) : "";
            if (!identifier(n.slot)) error(at(p, "slot"), "slot", "a slot has a name");
            else if (!slots_.insert(n.slot).second) error(at(p, "slot"), "slot", std::format("two slots are named `{}`", n.slot));
            return n;
        }
        return element(v, p, scope, row);
    }

    Node element(const Value& v, const std::string& p, const Scope& scope, bool row) {
        Node n;
        n.pointer = p;
        n.kind = Node::Kind::element;
        unknown_fields(v, p, std::array<std::string_view, 7>{"type", "id", "props", "on", "animate", "children", "notes"});
        const Value& type = *v.get("type");
        n.type = type.type() == Value::Type::text ? std::string(type.as_string()) : "";
        n.component = type_named(n.type);
        if (!n.component) {
            std::string hint;
            if (const std::string s = snake(n.type); s != n.type && type_named(s)) hint = std::format("component names are lower case: `{}`", s);
            else {
                std::vector<std::string> names;
                for (const ComponentType* t : registered_types()) names.emplace_back(t->name);
                hint = nearest(n.type, names);
                if (hint.empty()) hint = "import the package that defines it, and register its components";
            }
            error(at(p, "type"), "unknown-type", std::format("no component is named `{}`", n.type), hint);
        }
        const spec::ContractInfo* contract = n.component ? n.component->contract : nullptr;

        if (const Value* id = v.get("id")) {
            if (row) error(at(p, "id"), "id", "a row's node is named by its row's key", "remove the id; the row's key is its id (S-8)");
            else if (id->type() != Value::Type::text || !spec::valid_explicit_segment(id->as_string()))
                error(at(p, "id"), "id", std::format("{} is not an id", id->json()), "1-128 of A-Z a-z 0-9 _ . - (spec S-8)");
            else n.id = std::string(id->as_string());
        }
        if (const Value* props = v.get("props")) {
            if (!props->map()) error(at(p, "props"), "props", "`props` is an object: name → value");
            else
                for (const auto& [name, value] : *props->map()) {
                    const std::string q = at(at(p, "props"), name);
                    const spec::PropInfo* info = contract ? contract->prop(name) : nullptr;
                    if (!info && !is_runtime_property(name)) {
                        // A type with no contract and no properties of its own
                        // (box, row, column …) takes the runtime's only.
                        if (contract || (n.component && !n.component->set_prop)) {
                            std::vector<std::string_view> names;
                            if (contract)
                                for (const spec::PropInfo& x : contract->props) names.push_back(x.name);
                            for (std::string_view x : runtime_properties()) names.push_back(x);
                            error(q, "unknown-property", std::format("`{}` has no property `{}`", n.type, name), nearest(name, names));
                        } else if (n.component) {
                            warning(q, "unknown-property", std::format("`{}` states no contract, so `{}` is not checked", n.type, name));
                        }
                    }
                    n.props.emplace_back(name, binding(value, q, scope, info, name));
                }
        }
        if (const Value* on = v.get("on")) {
            if (!on->map()) error(at(p, "on"), "event", "`on` is an object: event → operations");
            else
                for (const auto& [event, list] : *on->map()) {
                    const std::string q = at(at(p, "on"), event);
                    const bool known = (contract && contract->event(event)) ||
                                       std::ranges::find(runtime_events(), event) != runtime_events().end();
                    if (!known && n.component) {
                        std::vector<std::string_view> names(runtime_events().begin(), runtime_events().end());
                        if (contract)
                            for (const spec::EventInfo& e : contract->events) names.push_back(e.name);
                        if (contract || !n.component->event) error(q, "unknown-event", std::format("`{}` has no event `{}`", n.type, event), nearest(event, names));
                        else warning(q, "unknown-event", std::format("`{}` states no contract, so `{}` is not checked", n.type, event));
                    }
                    n.on.push_back({event, ops(list, q, scope, doc_.component)});
                }
        }
        // An activation invokes one verb, through the node's `verb` (S-1).
        std::size_t verbs = std::ranges::any_of(n.props, [](const auto& p) { return p.first == "verb"; }) ? 1 : 0;
        for (const Reaction& r : n.on)
            if (r.event == "activate" || r.event == "activated" || r.event == "press")
                for (const Op& op : r.ops)
                    if (op.kind == Op::Kind::verb && ++verbs > 1)
                        error(op.pointer, "verb", "an activation invokes one verb",
                              "the node's verb classifies its activation (S-1); invoke the others from that verb");
        if (const Value* animate = v.get("animate")) {
            if (!animate->map()) error(at(p, "animate"), "transition", "`animate` is an object: property → transition");
            else
                for (const auto& [prop, t] : *animate->map())
                    if (auto x = transition(t, at(at(p, "animate"), prop))) n.animate.emplace_back(prop, *x);
        }
        if (const Value* children = v.get("children")) n.children = list(*children, at(p, "children"), scope);
        // An interactive node is addressed by an explicit id (S-8).
        const bool interactive = n.component && (n.component->focusable || (contract && !contract->actions.empty()) || !n.on.empty());
        if (interactive && n.id.empty() && !row)
            warning(p, "missing-id", std::format("this `{}` can be operated but has no id", n.type),
                    "give it an \"id\": agents and tests address it by its path (S-8)");
        return n;
    }

    std::vector<Node> list(const Value& v, const std::string& p, const Scope& scope) {
        std::vector<Node> out;
        if (!v.list()) {
            error(p, "children", "children are a list of nodes");
            return out;
        }
        std::set<std::string> ids;
        for (std::size_t i = 0; i < v.list()->size(); ++i) {
            Node n = node((*v.list())[i], at(p, i), scope, false);
            if (!n.id.empty() && !ids.insert(n.id).second)
                error(at(at(p, i), "id"), "duplicate-id", std::format("two children are named `{}`", n.id), "ids are unique among siblings (S-8)");
            out.push_back(std::move(n));
        }
        return out;
    }

    Node when(const Value& v, const std::string& p, const Scope& scope) {
        Node n;
        n.kind = Node::Kind::when;
        n.pointer = p;
        unknown_fields(v, p, std::array<std::string_view, 4>{"if", "then", "else", "notes"});
        if (auto e = expression(*v.get("if"), at(p, "if"), scope)) n.condition = std::move(*e);
        if (const Value* then = v.get("then")) n.children = list(*then, at(p, "then"), scope);
        else error(p, "if", "an `if` has a `then`", R"("then": [ … ])");
        if (const Value* otherwise = v.get("else")) n.otherwise = list(*otherwise, at(p, "else"), scope);
        return n;
    }

    Node each(const Value& v, const std::string& p, const Scope& scope) {
        Node n;
        n.kind = Node::Kind::each;
        n.pointer = p;
        unknown_fields(v, p, std::array<std::string_view, 5>{"for", "in", "where", "body", "notes"});
        const Value& var = *v.get("for");
        n.variable = var.type() == Value::Type::text ? std::string(var.as_string()) : "";
        if (!identifier(n.variable) || std::ranges::find(reserved, n.variable) != reserved.end())
            error(at(p, "for"), "for", "`for` names each row", R"("for": "note")");
        else if (scope.has(n.variable))
            warning(at(p, "for"), "shadow", std::format("`{}` hides another name in the loop", n.variable));
        const Value* in = v.get("in");
        n.model = in && in->type() == Value::Type::text ? std::string(in->as_string()) : "";
        if (std::ranges::find(doc_.models, n.model, &std::pair<std::string, std::string>::first) == doc_.models.end())
            error(in ? at(p, "in") : p, "unknown-model", std::format("`{}` is not a model of this document", n.model),
                  R"(a loop goes over a model: declare it in "models")");
        Scope inner = scope;
        inner.names.push_back(n.variable);
        if (const Value* where = v.get("where")) {
            n.filtered = true;
            if (auto e = expression(*where, at(p, "where"), inner)) n.condition = std::move(*e);
        }
        const Value* body = v.get("body");
        if (!body || !body->list() || body->list()->size() != 1) {
            error(body ? at(p, "body") : p, "body", "a loop's `body` is one node: the view of a row", R"("body": [{"type": "text", …}])");
            return n;
        }
        Node row = node((*body->list())[0], at(at(p, "body"), 0), inner, true);
        if (row.kind != Node::Kind::element) error(at(at(p, "body"), 0), "body", "a row's node is a component");
        n.children.push_back(std::move(row));
        return n;
    }

    std::string_view       text_;
    const ReadOptions&     o_;
    std::vector<Diagnostic> diags_;
    Document               doc_;
    std::set<std::string>  taken_;
    std::set<std::string>  slots_;
};

}  // namespace

std::string Diagnostic::text(std::string_view file) const {
    std::string out;
    if (!file.empty()) out += std::format("{}:", file);
    if (line) out += std::format("{}:{}:", line, column);
    if (!out.empty()) out += ' ';
    out += std::format("{}: {}", level == Level::error ? "error" : "warning", message);
    if (!hint.empty()) out += std::format(" ({})", hint);
    if (!code.empty()) out += std::format(" [{}]", code);
    return out;
}

Value Diagnostic::to_value(std::string_view file) const {
    Value v(Value::Map{});
    v.set("level", level == Level::error ? "error" : "warning").set("code", code);
    if (!file.empty()) v.set("file", file);
    v.set("pointer", pointer).set("line", static_cast<std::int64_t>(line)).set("column", static_cast<std::int64_t>(column));
    v.set("message", message);
    if (!hint.empty()) v.set("hint", hint);
    return v;
}

const InterfaceProp* Interface::prop(std::string_view name) const noexcept {
    auto it = std::ranges::find(props, name, &InterfaceProp::name);
    return it == props.end() ? nullptr : &*it;
}

const InterfaceEvent* Interface::event(std::string_view name) const noexcept {
    auto it = std::ranges::find(events, name, &InterfaceEvent::name);
    return it == events.end() ? nullptr : &*it;
}

const SignalDecl* Document::signal(std::string_view name) const noexcept {
    auto it = std::ranges::find(signals, name, &SignalDecl::name);
    return it == signals.end() ? nullptr : &*it;
}

std::size_t ReadResult::errors() const noexcept {
    return static_cast<std::size_t>(std::ranges::count(diagnostics, Diagnostic::Level::error, &Diagnostic::level));
}

ReadResult read(std::string_view json, const ReadOptions& options) { return Reader(json, options).run(); }

std::optional<Value> coerce(std::string_view type, const Value& v) {
    using T = Value::Type;
    if (type == "any") return v;
    if (type == "bool") return v.type() == T::boolean ? std::optional(v) : std::nullopt;
    if (type == "int") {
        if (v.type() == T::integer) return v;
        if (v.type() == T::real) {
            const double d = v.as_double();
            if (d == std::floor(d) && d >= -9.2e18 && d <= 9.2e18) return Value(static_cast<std::int64_t>(d));
        }
        return std::nullopt;
    }
    if (type == "double") return v.is_number() ? std::optional(Value(v.as_double())) : std::nullopt;
    if (type == "string") return v.type() == T::text ? std::optional(v) : std::nullopt;
    if (type == "list") return v.list() ? std::optional(v) : std::nullopt;
    if (type == "map") return v.map() ? std::optional(v) : std::nullopt;
    return std::nullopt;
}

}  // namespace omui::viewdoc
