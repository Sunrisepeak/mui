// A view document running: elements from the document, its state, reloads,
// and the components documents define.
module openmui.viewdoc;

import std;
import openmui.core;
import openmui.view;
import openmui.expr;
import openmui.trace;
import :document;
import :instance;

namespace omui::viewdoc {

namespace {

constexpr Log log{"viewdoc"};

// What a document's elements read and write: a view's state, or one
// component instance's.
struct Shared {
    Services*                                          services = nullptr;
    std::shared_ptr<const Document>                    doc;
    std::string                                        source;  // the text, to place diagnostics
    LoadOptions                                        options;
    std::map<std::string, Signal<Value>, std::less<>>  signals;
    std::map<std::string, Signal<Value>, std::less<>>  props;  // a component's interface
    std::map<std::string, Element, std::less<>>        slots;
    Signal<std::uint64_t>                              slots_revision{0};
    std::vector<std::pair<std::string, std::function<void(const Value&)>>> listeners;
    std::vector<std::function<void(std::string_view, const Value&)>>       forwarders;
    NodeId                                             self{};  // a component instance's node
    bool                                               component = false;
    std::set<std::string>                              reported;
    std::map<std::string, Watch, std::less<>>          filters;  // a filtered list's model, kept up to date
    std::vector<Diagnostic>                            warnings;
};

struct Env {
    std::shared_ptr<Shared>                     st;
    std::shared_ptr<const Document>             doc;  // the document the elements come from (kept while they live)
    std::vector<std::pair<std::string, Value>>  locals;
};

std::optional<std::string> read_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

Diagnostic unreadable(const std::filesystem::path& file) {
    Diagnostic d;
    d.code = "file";
    d.message = std::format("cannot read {}", utf8(file));
    return d;
}

// Once per place and message: an expression that fails every frame logs once.
void report(const Shared& st, std::string_view pointer, std::string_view message, std::string_view hint = {}) {
    auto& seen = const_cast<std::set<std::string>&>(st.reported);
    if (!seen.insert(std::format("{}\n{}", pointer, message)).second) return;
    Diagnostic d;
    d.pointer = std::string(pointer);
    d.message = std::string(message);
    d.hint = std::string(hint);
    d.code = "evaluation";
    if (auto place = json_location(st.source, pointer)) {
        d.line = static_cast<std::uint32_t>(place->line);
        d.column = static_cast<std::uint32_t>(place->column);
    }
    log.warn("{}", d.text(st.options.file));
}

std::optional<Value> lookup(const Env& env, std::string_view name) {
    for (auto it = env.locals.rbegin(); it != env.locals.rend(); ++it)
        if (it->first == name) return it->second;
    if (auto p = env.st->props.find(name); p != env.st->props.end()) return p->second.get();
    if (auto s = env.st->signals.find(name); s != env.st->signals.end()) return s->second.get();
    for (const auto& [model, type] : env.doc->models)
        if (model == name) {
            Model& m = env.st->services->model(model);
            (void)m.revision();  // a dependency: the binding follows the model
            return m.to_value();
        }
    return std::nullopt;
}

std::optional<Value> eval(const Env& env, const expr::Expr& e, std::string_view pointer) {
    expr::Context ctx;
    ctx.translate = env.st->options.translate;
    auto r = expr::evaluate(e, [&env](std::string_view n) { return lookup(env, n); }, ctx);
    if (!r.ok()) {
        report(*env.st, pointer, r.errors[0].message, r.errors[0].hint);
        return std::nullopt;
    }
    return std::move(r.value);
}

bool truthy(const Env& env, const expr::Expr& e, std::string_view pointer) {
    auto v = eval(env, e, pointer);
    if (!v) return false;
    if (v->type() != Value::Type::boolean) {
        report(*env.st, pointer, std::format("the condition is a {}, not a bool", spec::name(v->type())), "compare it: x != null, size(s) > 0");
        return false;
    }
    return v->as_bool();
}

Value default_of(const spec::PropInfo& p) {
    using K = spec::DefaultValue::Kind;
    const spec::DefaultValue& d = p.default_value;
    switch (d.kind) {
        case K::boolean: return Value(d.b);
        case K::integer: return Value(d.i);
        case K::real: return Value(d.d);
        case K::text: return Value(d.s);
        case K::list: {
            Value::List l;
            for (std::string_view s : d.list) l.emplace_back(s);
            return Value(std::move(l));
        }
        case K::none: break;
    }
    return {};
}

Value row_value(const Row& row) {
    Value v(Value::Map{});
    v.set("key", row.key);
    if (const Value::Map* m = row.fields.map())
        for (const auto& [k, x] : *m)
            if (k != "key") v.set(k, x);
    return v;
}

Value evaluated_args(const Env& env, const Op& op) {
    Value args(Value::Map{});
    for (const auto& [name, e] : op.args)
        if (auto v = eval(env, e, std::format("{}/args/{}", op.pointer, name))) args.set(name, std::move(*v));
    return args;
}

std::string envelope_error(const Envelope& e) {
    for (const spec::Record& r : e.records())
        if (r.kind == spec::RecordKind::error)
            if (const Value* m = r.body.get("message")) return std::string(m->as_string());
    return std::format("exit code {}", e.exit_code().value_or(1));
}

void write_prop(const Shared& st, std::string_view name, const Value& v) {
    auto it = const_cast<Shared&>(st).props.find(name);
    if (it == st.props.end()) return;
    it->second.set(v);
    if (st.services && st.self) st.services->write_back(st.self, name, v);
}

void write_signal(const Shared& st, std::string_view name, const Value& v, std::string_view pointer) {
    auto it = st.signals.find(name);
    if (it == st.signals.end()) return;
    const SignalDecl* decl = st.doc->signal(name);
    auto c = coerce(decl ? std::string_view(decl->type) : "any", v);
    if (!c) {
        report(st, pointer, std::format("`{}` is a {}; {} is not", name, decl ? decl->type : "any", v.json()));
        return;
    }
    it->second.set(std::move(*c));
}

void run(const Env& env, const std::vector<const Op*>& ops, const Value& args) {
    Env inner = env;
    inner.locals.emplace_back("event", args);
    batch([&] {
        for (const Op* op : ops) {
            switch (op->kind) {
                case Op::Kind::set: {
                    auto v = eval(inner, op->value, op->pointer);
                    if (!v) break;
                    if (inner.st->props.contains(op->target)) write_prop(*inner.st, op->target, *v);
                    else write_signal(*inner.st, op->target, *v, op->pointer);
                    break;
                }
                case Op::Kind::verb: {
                    Invocation call;
                    call.args = evaluated_args(inner, *op);
                    const Envelope r = inner.st->services->verbs().invoke(op->target, call);
                    if (r.exit_code().value_or(0) != 0)
                        report(*inner.st, op->pointer, std::format("`{}`: {}", op->target, envelope_error(r)),
                               "a verb that needs an answer belongs on an activation, where the action asks for it");
                    break;
                }
                case Op::Kind::emit: {
                    const Value a = evaluated_args(inner, *op);
                    if (inner.st->component) {
                        if (inner.st->self) inner.st->services->emit(inner.st->self, op->target, a);
                    } else {
                        for (const auto& [event, handler] : inner.st->listeners)
                            if (event == op->target) handler(a);
                        for (const auto& forward : inner.st->forwarders) forward(op->target, a);
                    }
                    break;
                }
            }
        }
    });
}

Source source(const Env& env, std::string_view name, const Binding& b, const spec::PropInfo* info) {
    switch (b.kind) {
        case Binding::Kind::constant: return Source{b.value, {}, {}};
        case Binding::Kind::expr:
        case Binding::Kind::tr: {
            // An error shows the property's default (the spec's rule) and logs once.
            Value fallback = info ? default_of(*info) : Value{};
            return Source{std::nullopt, [env, &b, fallback] { return eval(env, b.expr, b.pointer).value_or(fallback); }, {}};
        }
        case Binding::Kind::bind: {
            auto st = env.st;
            if (auto p = st->props.find(b.name); p != st->props.end()) {
                Signal<Value> s = p->second;
                return Source{std::nullopt, [s] { return s.get(); },
                              [st, n = b.name](const Value& v) { write_prop(*st, n, v); }};
            }
            auto it = st->signals.find(b.name);
            if (it == st->signals.end()) return Source{Value{}, {}, {}};
            Signal<Value> s = it->second;
            return Source{std::nullopt, [s] { return s.get(); },
                          [st, n = b.name, p = b.pointer](const Value& v) { write_signal(*st, n, v, p); }};
        }
    }
    (void)name;
    return {};
}

Element build(const Node& n, const Env& env);

// The model of a filtered list: the rows of `in` that `where` keeps, kept up
// to date by a watch (it follows the rows and whatever `where` reads).
std::string filtered_model(const Node& loop, const Env& env) {
    const std::string name = std::format("{}?{}", loop.model, loop.pointer);
    Shared& st = *env.st;
    // The state owns the watch: it holds the state weakly (no cycle).
    std::weak_ptr<Shared> weak = env.st;
    Env without = env;
    without.st.reset();
    st.filters.insert_or_assign(name, Watch([weak, without, &loop, name] {
        Env env = without;
        env.st = weak.lock();
        if (!env.st) return;
        Model& base = env.st->services->model(loop.model);
        (void)base.revision();
        std::vector<Row> keep;
        for (const Row& r : base.rows()) {
            Env inner = env;
            inner.locals.emplace_back(loop.variable, row_value(r));
            if (truthy(inner, loop.condition, loop.pointer + "/where")) keep.push_back(r);
        }
        untracked([&] {
            Model& out = env.st->services->model(name);
            const bool same = out.size() == keep.size() && std::ranges::equal(out.rows(), keep, [](const Row& a, const Row& b) {
                                  return a.key == b.key && a.fields == b.fields;
                              });
            if (same) return;
            batch([&] {
                out.clear();
                if (auto r = out.push(std::move(keep)); !r) log.warn("{}: {}", name, r.error());
            });
        });
    }));
    return name;
}

void append(std::vector<Element>& out, const Node& n, const Env& env) {
    switch (n.kind) {
        case Node::Kind::element: out.push_back(build(n, env)); return;
        case Node::Kind::when: {
            // A computed condition: the region rebuilds when it flips, not
            // whenever what it reads changes.
            Computed<bool> cond([env, &n] { return truthy(env, n.condition, n.pointer + "/if"); });
            out.push_back(dynamic([env, &n, cond] {
                std::vector<Element> children;
                const bool yes = cond.get();
                untracked([&] {
                    for (const Node& c : yes ? n.children : n.otherwise) append(children, c, env);
                });
                return children;
            }));
            return;
        }
        case Node::Kind::each:
            out.push_back(dynamic([env, &n] {
                std::vector<Element> rows;
                if (n.children.empty()) return rows;
                Model& m = env.st->services->model(n.model);
                (void)m.revision();
                for (const Row& r : m.rows()) {
                    Env inner = env;
                    inner.locals.emplace_back(n.variable, row_value(r));
                    if (n.filtered && !truthy(inner, n.condition, n.pointer + "/where")) continue;
                    Element row = untracked([&] { return build(n.children[0], inner); });
                    row.id = r.key;  // a row's node is named by its key (S-8)
                    row.key = r.key;
                    rows.push_back(std::move(row));
                }
                return rows;
            }));
            return;
        case Node::Kind::slot: {
            auto st = env.st;
            out.push_back(dynamic([st, name = n.slot] {
                (void)st->slots_revision.get();
                std::vector<Element> filled;
                if (auto it = st->slots.find(name); it != st->slots.end()) {
                    filled.push_back(it->second);
                } else if (st->services) {
                    // Not filled by code: the slot is an outlet — a view mounted
                    // under its name (another document, a script's) shows here.
                    if (const auto& v = st->services->outlet(name).get()) filled.push_back(*v);
                }
                return filled;
            }));
            return;
        }
    }
}

Element build(const Node& n, const Env& env) {
    if (n.kind != Node::Kind::element) {
        // A root that is not a component: a box around it.
        Element box;
        std::vector<Element> inside;
        append(inside, n, env);
        box.children = std::move(inside);
        return box;
    }
    Element e;
    e.type = n.component;
    e.id = n.id;
    const spec::ContractInfo* contract = e.type ? e.type->contract : nullptr;
    for (const auto& [name, b] : n.props) e.prop(name, source(env, name, b, contract ? contract->prop(name) : nullptr));
    for (const Reaction& h : n.on) {
        const bool activation = h.event == "activate" || h.event == "activated" || h.event == "press";
        std::vector<const Op*> ops;
        for (const Op& op : h.ops) {
            if (activation && op.kind == Op::Kind::verb) {
                // The node's verb: its effect classifies the activation and the
                // runtime asks for its answer (S-1, S-2) before it runs.
                e.prop("verb", Source{Value(op.target), {}, {}});
                if (!op.args.empty())
                    e.prop("verb_args", Source{std::nullopt, [env, &op] { return evaluated_args(env, op); }, {}});
                continue;
            }
            ops.push_back(&op);
        }
        if (!ops.empty()) e.on(h.event, [env, ops](const Value& args) { run(env, ops, args); });
    }
    for (const auto& [prop, t] : n.animate) e.animate(prop, t);
    if (e.type && e.type->set_item && n.children.size() == 1 && n.children[0].kind == Node::Kind::each) {
        // A list shows a loop's rows itself, virtualised (S-7): the loop's
        // body is its item's view.
        const Node& loop = n.children[0];
        const std::string model = loop.filtered ? filtered_model(loop, env) : loop.model;
        if (std::ranges::find(n.props, std::string_view("model"), &std::pair<std::string, Binding>::first) == n.props.end())
            e.prop("model", Source{Value(model), {}, {}});
        std::function<Element(const Row&, std::size_t)> item = [env, &loop](const Row& row, std::size_t) {
            Env inner = env;
            inner.locals.emplace_back(loop.variable, row_value(row));
            return build(loop.children[0], inner);
        };
        e.configure = [set = e.type->set_item, item](void* instance) { set(instance, item); };
        return e;
    }
    for (const Node& c : n.children) append(e.children, c, env);
    return e;
}

Element root_element(const std::shared_ptr<Shared>& st) { return build(st->doc->root, Env{st, st->doc, {}}); }

// The document's signals: its own, and those the host provides.
std::expected<void, std::vector<Diagnostic>> init_signals(Shared& st) {
    std::vector<Diagnostic> missing;
    for (const SignalDecl& d : st.doc->signals) {
        if (!d.provided) {
            st.signals.insert_or_assign(d.name, Signal<Value>(d.value));
            continue;
        }
        auto s = st.options.provide ? st.options.provide(d.name) : std::nullopt;
        if (!s) {
            Diagnostic m;
            m.code = "provided";
            m.pointer = d.pointer;
            m.message = std::format("the host provides no signal `{}`", d.name);
            if (auto place = json_location(st.source, d.pointer)) {
                m.line = static_cast<std::uint32_t>(place->line);
                m.column = static_cast<std::uint32_t>(place->column);
            }
            missing.push_back(std::move(m));
            continue;
        }
        st.signals.insert_or_assign(d.name, *s);
    }
    if (!missing.empty()) return std::unexpected(std::move(missing));
    return {};
}

void log_warnings(const std::vector<Diagnostic>& diags, std::string_view file) {
    for (const Diagnostic& d : diags)
        if (d.level == Diagnostic::Level::warning) log.warn("{}", d.text(file));
}

std::vector<Diagnostic> warnings_of(std::vector<Diagnostic> diags) {
    std::erase_if(diags, [](const Diagnostic& d) { return d.level != Diagnostic::Level::warning; });
    return diags;
}

}  // namespace

// ── views ──────────────────────────────────────────────────────────────────

struct View::State : Shared {
    Signal<std::uint64_t> revision{0};
    Runtime*              runtime = nullptr;
    std::string           mounted;
};

std::expected<View, std::vector<Diagnostic>> View::load(Services& services, std::string_view json, LoadOptions options) {
    trace::Span span("viewdoc.load", "viewdoc");
    ReadResult r = read(json, {.file = options.file, .verbs = &services.verbs()});
    if (!r.ok()) return std::unexpected(std::move(r.diagnostics));
    if (r.document->component) {
        Diagnostic d;
        d.code = "document";
        d.message = "a component document is defined (define_component), not loaded as a view";
        return std::unexpected(std::vector{std::move(d)});
    }
    log_warnings(r.diagnostics, options.file);
    auto st = std::make_shared<State>();
    st->services = &services;
    st->doc = std::make_shared<const Document>(std::move(*r.document));
    st->source = std::string(json);
    st->options = std::move(options);
    st->warnings = warnings_of(std::move(r.diagnostics));
    if (auto ok = init_signals(*st); !ok) return std::unexpected(std::move(ok.error()));
    View v;
    v.state_ = std::move(st);
    return v;
}

std::expected<View, std::vector<Diagnostic>> View::load_file(Services& services, const std::filesystem::path& file, LoadOptions options) {
    auto text = read_file(file);
    if (!text) return std::unexpected(std::vector{unreadable(file)});
    if (options.file.empty()) options.file = utf8(file);
    return load(services, *text, std::move(options));
}

View& View::slot(std::string_view name, Element content) {
    state_->slots.insert_or_assign(std::string(name), std::move(content));
    state_->slots_revision.set(state_->slots_revision.peek() + 1);
    return *this;
}

View& View::on(std::string event, std::function<void(const Value& args)> handler) {
    state_->listeners.emplace_back(std::move(event), std::move(handler));
    return *this;
}

View& View::on_any(std::function<void(std::string_view event, const Value& args)> handler) {
    state_->forwarders.push_back(std::move(handler));
    return *this;
}

Element View::element() const {
    std::shared_ptr<State> st = state_;
    return dynamic([st] {
        (void)st->revision.get();
        return untracked([&] { return std::vector<Element>{root_element(st)}; });
    });
}

NodeId View::mount(Runtime& runtime, std::string_view slot) {
    state_->runtime = &runtime;
    state_->mounted = std::string(slot);
    return runtime.mount(slot, root_element(state_));
}

std::expected<void, std::vector<Diagnostic>> View::reload(std::string_view json) {
    trace::Span span("viewdoc.reload", "viewdoc");
    State& st = *state_;
    ReadResult r = read(json, {.file = st.options.file, .verbs = &st.services->verbs()});
    if (!r.ok()) return std::unexpected(std::move(r.diagnostics));
    if (r.document->component) {
        Diagnostic d;
        d.code = "document";
        d.message = "the view's new text is a component document";
        return std::unexpected(std::vector{std::move(d)});
    }
    log_warnings(r.diagnostics, st.options.file);
    auto doc = std::make_shared<const Document>(std::move(*r.document));
    // Signals of the same name and type keep their state (and identity).
    std::map<std::string, Signal<Value>, std::less<>> next;
    for (const SignalDecl& d : doc->signals) {
        if (d.provided) {
            auto s = st.options.provide ? st.options.provide(d.name) : std::nullopt;
            if (!s) {
                Diagnostic m;
                m.code = "provided";
                m.pointer = d.pointer;
                m.message = std::format("the host provides no signal `{}`", d.name);
                return std::unexpected(std::vector{std::move(m)});
            }
            next.insert_or_assign(d.name, *s);
            continue;
        }
        const SignalDecl* before = st.doc->signal(d.name);
        auto old = st.signals.find(d.name);
        if (before && !before->provided && before->type == d.type && old != st.signals.end()) next.insert_or_assign(d.name, old->second);
        else next.insert_or_assign(d.name, Signal<Value>(d.value));
    }
    st.signals = std::move(next);
    st.doc = std::move(doc);
    st.source = std::string(json);
    st.warnings = warnings_of(std::move(r.diagnostics));
    st.reported.clear();
    st.filters.clear();
    batch([&] { st.revision.set(st.revision.peek() + 1); });
    if (st.runtime) st.runtime->mount(st.mounted, root_element(state_));
    return {};
}

std::expected<void, std::vector<Diagnostic>> View::reload_file(const std::filesystem::path& file) {
    auto text = read_file(file);
    if (!text) return std::unexpected(std::vector{unreadable(file)});
    return reload(*text);
}

std::optional<Signal<Value>> View::signal(std::string_view name) const {
    auto it = state_->signals.find(name);
    if (it == state_->signals.end()) return std::nullopt;
    return it->second;
}

const Document& View::document() const noexcept { return *state_->doc; }

std::span<const Diagnostic> View::warnings() const noexcept { return state_->warnings; }

// ── components ─────────────────────────────────────────────────────────────

namespace {

struct Instance;

// A component a document defines: its type, and the contract generated from
// its interface (the strings live in the document, kept by the definition).
struct Definition {
    std::string                                name;
    std::shared_ptr<const Document>            doc;
    std::string                                source;
    LoadOptions                                options;
    ComponentType                              type;
    spec::ContractInfo                         contract{};
    std::vector<spec::PropInfo>                props;
    std::vector<spec::EventInfo>               events;
    std::vector<std::vector<spec::ArgInfo>>    args;
    std::vector<std::vector<std::string_view>> values;
    std::set<Instance*>                        live;

    void build_contract();
};

struct Instance {
    Definition*             def;
    std::shared_ptr<Shared> st = std::make_shared<Shared>();

    explicit Instance(Definition* d) : def(d) { reset(); }

    // The state of the definition's current document: props keep their
    // values when the definition changes; signals of the same name and type too.
    void reset() {
        st->component = true;
        st->options = def->options;
        std::map<std::string, Signal<Value>, std::less<>> props, signals;
        for (const InterfaceProp& p : def->doc->interface.props) {
            auto old = st->props.find(p.name);
            props.insert_or_assign(p.name, old != st->props.end() ? old->second : Signal<Value>(p.default_value));
        }
        for (const SignalDecl& d : def->doc->signals) {
            const SignalDecl* before = st->doc ? st->doc->signal(d.name) : nullptr;
            auto old = st->signals.find(d.name);
            signals.insert_or_assign(d.name, before && before->type == d.type && old != st->signals.end() ? old->second : Signal<Value>(d.value));
        }
        st->props = std::move(props);
        st->signals = std::move(signals);
        st->doc = def->doc;
        st->source = def->source;
        st->reported.clear();
        st->filters.clear();
    }

    std::vector<Element> compose() {
        if (!st->services) return {};
        return untracked([&] { return std::vector<Element>{build(st->doc->root, Env{st, st->doc, {}})}; });
    }
};

std::map<std::string, std::unique_ptr<Definition>, std::less<>>& definitions() {
    static std::map<std::string, std::unique_ptr<Definition>, std::less<>> all;
    return all;
}

spec::PropType prop_type(std::string_view t) {
    if (t == "bool") return spec::PropType::boolean;
    if (t == "i32") return spec::PropType::i32;
    if (t == "i64") return spec::PropType::i64;
    if (t == "f64") return spec::PropType::f64;
    if (t == "string") return spec::PropType::string;
    if (t == "color") return spec::PropType::color;
    if (t == "enum") return spec::PropType::choice;
    if (t == "list<string>") return spec::PropType::string_list;
    return spec::PropType::any;
}

void Definition::build_contract() {
    const Interface& in = doc->interface;
    props.clear();
    values.clear();
    values.reserve(in.props.size());
    for (const InterfaceProp& p : in.props) {
        std::vector<std::string_view>& v = values.emplace_back();
        for (const std::string& x : p.values) v.push_back(x);
        spec::DefaultValue def;
        switch (p.default_value.type()) {
            case Value::Type::boolean: def = spec::DefaultValue::of_bool(p.default_value.as_bool()); break;
            case Value::Type::integer: def = spec::DefaultValue::of_int(p.default_value.as_int()); break;
            case Value::Type::real: def = spec::DefaultValue::of_double(p.default_value.as_double()); break;
            case Value::Type::text: def = spec::DefaultValue::of_string(p.default_value.as_string()); break;
            default: break;
        }
        props.push_back({p.name, prop_type(p.type), v, def, p.bindable, p.doc});
    }
    events.clear();
    args.clear();
    args.reserve(in.events.size());
    for (const InterfaceEvent& e : in.events) {
        std::vector<spec::ArgInfo>& a = args.emplace_back();
        for (const auto& [n, t] : e.args) a.push_back({n, t});
        events.push_back({e.name, a, false, e.doc});
    }
    contract = spec::ContractInfo{name, in.role, format_version, props, events, {}, {}, {}, {}, {}, in.doc};
    type.contract = &contract;
    type.role = in.role;
}

void fill_type(Definition& d) {
    ComponentType& t = d.type;
    t.name = d.name;
    t.context = &d;
    t.create_in = [](const void* c) -> void* {
        auto* def = static_cast<Definition*>(const_cast<void*>(c));
        auto* i = new Instance(def);
        def->live.insert(i);
        return i;
    };
    t.destroy = [](void* p) {
        auto* i = static_cast<Instance*>(p);
        i->def->live.erase(i);
        delete i;
    };
    t.mount = [](void* p, Services& s, NodeId n) {
        auto* i = static_cast<Instance*>(p);
        i->st->services = &s;
        i->st->self = n;
    };
    t.set_prop = [](void* p, std::string_view k, const Value& v) {
        auto* i = static_cast<Instance*>(p);
        auto it = i->st->props.find(k);
        if (it == i->st->props.end()) return false;
        it->second.set(v);
        return true;
    };
    t.get_prop = [](const void* p, std::string_view k) -> Value {
        const auto* i = static_cast<const Instance*>(p);
        auto it = i->st->props.find(k);
        return it == i->st->props.end() ? Value{} : it->second.peek();
    };
    t.compose = [](void* p) { return static_cast<Instance*>(p)->compose(); };
}

}  // namespace

std::expected<const ComponentType*, std::vector<Diagnostic>> define_component(std::string_view json, LoadOptions options) {
    trace::Span span("viewdoc.define", "viewdoc");
    ReadResult r = read(json, {.file = options.file});
    if (!r.ok()) return std::unexpected(std::move(r.diagnostics));
    if (!r.document->component) {
        Diagnostic d;
        d.code = "document";
        d.message = "the document is a view; a component document has \"component\" with an interface";
        return std::unexpected(std::vector{std::move(d)});
    }
    log_warnings(r.diagnostics, options.file);
    auto doc = std::make_shared<const Document>(std::move(*r.document));
    auto& all = definitions();
    auto it = all.find(doc->name);
    if (it == all.end()) {
        if (const ComponentType* existing = find_type(doc->name)) {
            Diagnostic d;
            d.code = "name";
            d.pointer = "/component/name";
            d.message = std::format("a component named `{}` exists already", existing->name);
            d.hint = "give the document's component another name";
            if (auto place = json_location(json, d.pointer)) {
                d.line = static_cast<std::uint32_t>(place->line);
                d.column = static_cast<std::uint32_t>(place->column);
            }
            return std::unexpected(std::vector{std::move(d)});
        }
        auto def = std::make_unique<Definition>();
        def->name = doc->name;
        it = all.emplace(def->name, std::move(def)).first;
        fill_type(*it->second);
    }
    Definition& def = *it->second;
    def.doc = std::move(doc);
    def.source = std::string(json);
    def.options = std::move(options);
    def.build_contract();
    register_type(def.type);
    // Instances follow: their props and state stay, their children are built again.
    for (Instance* i : def.live) {
        i->reset();
        if (i->st->services && i->st->self) i->st->services->recompose(i->st->self);
    }
    return &def.type;
}

std::expected<const ComponentType*, std::vector<Diagnostic>> define_component_file(const std::filesystem::path& file, LoadOptions options) {
    auto text = read_file(file);
    if (!text) return std::unexpected(std::vector{unreadable(file)});
    if (options.file.empty()) options.file = utf8(file);
    return define_component(*text, std::move(options));
}

Element make(std::string_view type, std::vector<std::pair<std::string, Source>> props, std::vector<std::pair<std::string, Handler>> handlers,
             std::string id) {
    Element e;
    e.type = find_type(type);
    if (!e.type) log.warn("no component is named `{}`", type);
    e.id = std::move(id);
    for (auto& [k, s] : props) e.prop(std::move(k), std::move(s));
    for (auto& [k, h] : handlers) e.on(std::move(k), std::move(h));
    return e;
}

}  // namespace omui::viewdoc
