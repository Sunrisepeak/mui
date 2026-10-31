module openmui.lang;

import std;
import nlohmann.json;
import openmui.abi;
import :host;
import :view;

namespace omui::lang {

namespace {

abi::OmuiString str(std::string_view s) { return {s.data(), s.size()}; }

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

void merge(std::vector<abi::OmuiSignal>& into, const std::vector<abi::OmuiSignal>& from) {
    for (abi::OmuiSignal s : from)
        if (std::ranges::find(into, s) == into.end()) into.push_back(s);
}

std::string signal_name(abi::OmuiSignal s) { return std::format("s{}", s); }

abi::OmuiValue json_value(const std::string& text) {
    abi::OmuiValue v{};
    v.type = abi::OMUI_VALUE_TYPE_JSON;
    v.s = str(text);
    return v;
}

std::vector<Json> records(std::string_view ndjson) {
    std::vector<Json> out;
    for (auto line : ndjson | std::views::split('\n')) {
        const std::string_view l(line.begin(), line.end());
        if (l.find_first_not_of(" \r\t") == std::string_view::npos) continue;
        Json v = Json::parse(l, nullptr, false);
        if (!v.is_discarded()) out.push_back(std::move(v));
    }
    return out;
}

Error failure(abi::OmuiResult r, std::string message = {}) { return Error{r, std::move(message)}; }

// JSON text of a script's value: a string that is not valid UTF-8 has its bad
// bytes replaced (U+FFFD) instead of throwing — a script's text is not trusted.
std::string dump(const Json& v) { return v.dump(-1, ' ', false, Json::error_handler_t::replace); }

}  // namespace

// ── expressions ───────────────────────────────────────────────────────────

Expr literal(const Json& value) { return {dump(value), {}}; }
Expr signal_ref(abi::OmuiSignal s) { return {signal_name(s), {s}}; }
Expr name_ref(std::string name) { return {std::move(name), {}}; }

Expr call(std::string_view function, std::vector<Expr> args) {
    Expr e{std::string(function) + "(", {}};
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) e.text += ", ";
        e.text += args[i].text;
        merge(e.signals, args[i].signals);
    }
    e.text += ")";
    return e;
}

Expr binary(std::string_view op, Expr a, Expr b) {
    Expr e{std::format("({} {} {})", a.text, op, b.text), std::move(a.signals)};
    merge(e.signals, b.signals);
    return e;
}

Expr unary(std::string_view op, Expr a) { return {std::format("{}({})", op, a.text), std::move(a.signals)}; }

Expr select(Expr cond, Expr then, Expr otherwise) {
    Expr e{std::format("({} ? {} : {})", cond.text, then.text, otherwise.text), std::move(cond.signals)};
    merge(e.signals, then.signals);
    merge(e.signals, otherwise.signals);
    return e;
}

Expr member(Expr object, std::string_view field) { return {std::format("{}.{}", object.text, field), std::move(object.signals)}; }

Expr fmt(std::string_view pattern, std::vector<Expr> args) {
    args.insert(args.begin(), literal(Json(pattern)));
    return call("format", std::move(args));
}

std::string contract_name(std::string_view s) {
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

// ── the element tree the bindings' own layers send ──────────────────────────────

namespace {

Expr expr_of(const Json& j) {
    Expr e{j.value("e", ""), {}};
    if (j.contains("s") && j["s"].is_array())
        for (const Json& s : j["s"]) e.signals.push_back(s.get<abi::OmuiSignal>());
    return e;
}

Result<Element> element_of(const Json& j, const std::function<std::optional<Handler>(std::size_t)>& handler, int depth) {
    auto bad = [](std::string m) { return std::unexpected(Error{abi::OMUI_ERROR_INVALID_ARGUMENT, std::move(m)}); };
    if (depth > 200) return bad("the element tree is deeper than 200 levels");
    if (!j.is_object()) return bad("an element is an object");
    auto list = [&](const Json& a, std::vector<Element>& out) -> Result<void> {
        if (!a.is_array()) return bad("children are a list");
        for (const Json& c : a) {
            auto x = element_of(c, handler, depth + 1);
            if (!x) return std::unexpected(x.error());
            out.push_back(std::move(*x));
        }
        return {};
    };
    Element e;
    if (j.contains("region")) {
        const Json& r = j["region"];
        auto region = std::make_shared<Region>();
        const std::string kind = r.value("kind", "");
        if (kind == "when") {
            region->kind = Region::Kind::when;
            region->condition = expr_of(r.value("cond", Json::object()));
            if (auto x = list(r.value("then", Json::array()), region->then); !x) return std::unexpected(x.error());
            if (auto x = list(r.value("else", Json::array()), region->otherwise); !x) return std::unexpected(x.error());
        } else if (kind == "each") {
            region->kind = Region::Kind::each;
            region->model = r.value("model", "");
            region->variable = r.value("variable", "row");
            if (r.contains("where") && r["where"].is_object()) region->condition = expr_of(r["where"]);
            auto body = element_of(r.value("body", Json::object()), handler, depth + 1);
            if (!body) return std::unexpected(body.error());
            region->then.push_back(std::move(*body));
        } else if (kind == "slot") {
            region->kind = Region::Kind::slot;
            region->slot = r.value("name", "");
        } else {
            return bad(std::format("no region `{}`", kind));
        }
        e.region = std::move(region);
        return e;
    }
    e.type = j.value("type", "");
    e.id = j.value("id", "");
    if (j.contains("props") && j["props"].is_object())
        for (auto it = j["props"].begin(); it != j["props"].end(); ++it) {
            const Json& p = it.value();
            Prop prop;
            if (p.contains("c")) {
                prop.value = p["c"];
            } else if (p.contains("b")) {
                prop.kind = Prop::Kind::bind;
                prop.signal = p["b"].get<abi::OmuiSignal>();
            } else if (p.contains("e")) {
                prop.kind = Prop::Kind::expr;
                prop.expr = expr_of(p);
            } else if (p.contains("t")) {
                prop.kind = Prop::Kind::tr;
                prop.value = p["t"];
                if (p.contains("a") && p["a"].is_object())
                    for (auto a = p["a"].begin(); a != p["a"].end(); ++a) prop.args.emplace_back(a.key(), expr_of(a.value()));
            } else {
                return bad(std::format("the property `{}` has no value", it.key()));
            }
            e.props.emplace_back(it.key(), std::move(prop));
        }
    if (j.contains("on") && j["on"].is_object())
        for (auto it = j["on"].begin(); it != j["on"].end(); ++it) {
            auto h = handler(it.value().get<std::size_t>());
            if (!h) return bad(std::format("the handler of `{}` is missing", it.key()));
            e.on.emplace_back(it.key(), std::move(*h));
        }
    if (j.contains("animate") && j["animate"].is_object())
        for (auto it = j["animate"].begin(); it != j["animate"].end(); ++it) e.animate.emplace_back(it.key(), it.value());
    if (j.contains("children"))
        if (auto x = list(j["children"], e.children); !x) return std::unexpected(x.error());
    return e;
}

}  // namespace

Result<Element> element_from_json(const Json& tree, const std::function<std::optional<Handler>(std::size_t)>& handler) {
    return element_of(tree, handler, 0);
}

// ── the document of an element tree ──────────────────────────────────────────

namespace {

struct Compiler {
    std::vector<abi::OmuiSignal>     signals;
    std::vector<std::string>         models;    // what its loops read
    std::map<std::string, Handler>*  handlers;  // by the event the document emits
    std::vector<std::string>         rows;      // loop variables in scope
    int                              next = 0;

    Json prop(const Prop& p) {
        switch (p.kind) {
            case Prop::Kind::constant:
                // A constant that could read as a binding stays a constant.
                if (p.value.is_object() && p.value.size() == 1 &&
                    (p.value.contains("expr") || p.value.contains("bind") || p.value.contains("tr") || p.value.contains("value")))
                    return Json{{"value", p.value}};
                return p.value;
            case Prop::Kind::expr:
                merge(signals, p.expr.signals);
                return Json{{"expr", p.expr.text}};
            case Prop::Kind::bind:
                merge(signals, {p.signal});
                return Json{{"bind", signal_name(p.signal)}};
            case Prop::Kind::tr: {
                Json args = Json::object();
                for (const auto& [name, a] : p.args) {
                    merge(signals, a.signals);
                    args[name] = a.text;
                }
                Json t{{"tr", p.value}};
                if (!args.empty()) t["args"] = std::move(args);
                return t;
            }
        }
        return nullptr;
    }

    Json nodes(const std::vector<Element>& list) {
        Json out = Json::array();
        for (const Element& e : list) out.push_back(node(e));
        return out;
    }

    Json node(const Element& e) {
        if (e.region) {
            const Region& r = *e.region;
            switch (r.kind) {
                case Region::Kind::when: {
                    merge(signals, r.condition.signals);
                    Json n{{"if", r.condition.text}, {"then", nodes(r.then)}};
                    if (!r.otherwise.empty()) n["else"] = nodes(r.otherwise);
                    return n;
                }
                case Region::Kind::each: {
                    if (std::ranges::find(models, r.model) == models.end()) models.push_back(r.model);
                    Json n{{"for", r.variable}, {"in", r.model}};
                    if (!r.condition.text.empty()) {
                        merge(signals, r.condition.signals);
                        n["where"] = r.condition.text;
                    }
                    rows.push_back(r.variable);
                    n["body"] = nodes(r.then);
                    rows.pop_back();
                    return n;
                }
                case Region::Kind::slot: return Json{{"slot", r.slot}};
            }
        }
        Json n{{"type", contract_name(e.type)}};
        if (!e.id.empty()) n["id"] = e.id;
        if (!e.props.empty()) {
            Json props = Json::object();
            for (const auto& [k, p] : e.props) props[k] = prop(p);
            n["props"] = std::move(props);
        }
        if (!e.on.empty()) {
            // A handler is the document's own event, forwarded to the script
            // with the event and the rows of the loops it is in.
            Json on = Json::object();
            for (const auto& [event, h] : e.on) {
                const std::string name = std::format("h{}", ++next);
                (*handlers)[name] = h;
                Json args{{"event", "event"}};
                if (!rows.empty()) {
                    std::string map = "{";
                    for (std::size_t i = 0; i < rows.size(); ++i) map += std::format("{}\"{}\": {}", i ? ", " : "", rows[i], rows[i]);
                    args["rows"] = map + "}";
                }
                on[event] = Json::array({Json{{"emit", name}, {"args", std::move(args)}}});
            }
            n["on"] = std::move(on);
        }
        if (!e.animate.empty()) {
            Json a = Json::object();
            for (const auto& [k, t] : e.animate) a[k] = t;
            n["animate"] = std::move(a);
        }
        if (!e.children.empty()) n["children"] = nodes(e.children);
        return n;
    }
};

}  // namespace

// ── the session ──────────────────────────────────────────────────────────

struct Session::VerbBox {
    Host*                                           host;
    std::function<Result<Json>(const Json&)>        fn;
    VerbSpec                                        spec;  // as registered
};

// The threads async natives run on: a queue, and a few workers.
struct Session::Workers {
    std::mutex                        mutex;
    std::condition_variable_any       wake;
    std::deque<std::function<void()>> jobs;
    std::vector<std::jthread>         threads;  // last: joined first, while the queue still exists

    explicit Workers(std::size_t n) {
        for (std::size_t i = 0; i < n; ++i)
            threads.emplace_back([this](std::stop_token stop) {
                while (true) {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(mutex);
                        if (!wake.wait(lock, stop, [&] { return !jobs.empty(); })) return;
                        job = std::move(jobs.front());
                        jobs.pop_front();
                    }
                    job();
                }
            });
    }

    void push(std::function<void()> job) {
        {
            std::scoped_lock lock(mutex);
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
    }
};

Session::Session(Host& host) : host_(host) {}

Session::~Session() {
    *alive_ = false;
    workers_.reset();  // a native call running now finishes; the queued ones are dropped
    // What the session subscribed to goes with it.
    auto rt = host_.runtime();
    std::vector<abi::OmuiSubscription> subs;
    for (auto& [s, m] : signals_)
        if (m.watch) subs.push_back(m.watch);
    for (auto& [slot, m] : mounted_) subs.insert(subs.end(), m.subscriptions.begin(), m.subscriptions.end());
    host_.ui([&] {
        for (abi::OmuiSubscription s : subs) (void)abi::omuiOff(rt, s);
    });
}

Result<abi::OmuiSignal> Session::signal(const Json& initial) {
    const std::string text = dump(initial);
    struct Watcher {
        Host*                 host;
        Session*              session;
        std::shared_ptr<bool> alive;
    };
    auto watcher = std::make_shared<Watcher>(Watcher{&host_, this, alive_});
    abi::OmuiSignal s = 0;
    abi::OmuiSubscription sub = 0;
    const abi::OmuiResult r = host_.ui([&] {
        const abi::OmuiValue v = json_value(text);
        abi::OmuiResult x = abi::omuiSignalCreate(host_.runtime(), &v, &s);
        if (x != abi::OMUI_SUCCESS) return x;
        const std::string name = signal_name(s);
        if ((x = abi::omuiSignalShare(host_.runtime(), s, str(name))) != abi::OMUI_SUCCESS) return x;
        return abi::omuiSignalWatch(
            host_.runtime(), s,
            [](void* user, abi::OmuiSignal signal, abi::OmuiString value) {
                auto* w = static_cast<Watcher*>(user);
                Json v = Json::parse(std::string_view(value.data, value.size), nullptr, false);
                std::weak_ptr<bool> alive = w->alive;
                Session* session = w->session;
                w->host->post([alive, session, signal, v = std::move(v)]() mutable {
                    if (auto a = alive.lock(); a && *a) session->changed(signal, std::move(v));
                });
            },
            watcher.get(), &sub);
    });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r, "a signal could not be made"));
    Mirror& m = signals_[s];
    m.value = initial;
    m.watch = sub;
    watchers_.push_back(std::move(watcher));
    return s;
}

const Json& Session::get(abi::OmuiSignal s) const {
    static const Json null;
    auto it = signals_.find(s);
    return it == signals_.end() ? null : it->second.value;
}

namespace {
std::uint64_t next_watch = 0;  // tokens: the script thread's alone
}  // namespace

void Session::changed(abi::OmuiSignal s, Json value) {
    auto it = signals_.find(s);
    if (it == signals_.end()) return;
    Mirror& m = it->second;
    if (!m.inflight.empty() && m.inflight.front() == value) return;  // the script's own write, coming back
    if (m.value == value) return;
    m.value = std::move(value);
    const Json now = m.value;  // a watcher may write again
    for (std::size_t i = 0; i < m.watchers.size(); ++i) m.watchers[i].second(now);
}

void Session::set(abi::OmuiSignal s, Json value) {
    const std::string text = dump(value);
    Mirror& m = signals_[s];
    m.inflight.push_back(value);
    if (m.value != value) {
        m.value = std::move(value);
        const Json now = m.value;
        for (std::size_t i = 0; i < m.watchers.size(); ++i) m.watchers[i].second(now);
    }
    auto rt = host_.runtime();
    Host* host = &host_;
    std::weak_ptr<bool> alive = alive_;
    host_.send([rt, s, text, host, alive, this] {
        const abi::OmuiValue v = json_value(text);
        (void)abi::omuiSignalSet(rt, s, &v);
        // After the runtime's echo (posted while it set), the write is no longer on its way.
        host->post([alive, this, s] {
            if (auto a = alive.lock(); !a || !*a) return;
            auto it = signals_.find(s);
            if (it != signals_.end() && !it->second.inflight.empty()) it->second.inflight.pop_front();
        });
    });
}

std::uint64_t Session::watch(abi::OmuiSignal s, std::function<void(const Json&)> fn) {
    const std::uint64_t token = ++next_watch;
    signals_[s].watchers.emplace_back(token, std::move(fn));
    return token;
}

void Session::unwatch(std::uint64_t token) {
    for (auto& [s, m] : signals_) std::erase_if(m.watchers, [&](const auto& w) { return w.first == token; });
}

namespace {

Json compile(const Element& root, std::map<std::string, Handler>& handlers) {
    Compiler c{{}, {}, &handlers, {}, 0};
    Json r = c.node(root);
    Json signals = Json::object();
    for (abi::OmuiSignal s : c.signals) signals[signal_name(s)] = Json{{"type", "any"}, {"provided", true}};
    Json view = Json::object();
    if (!signals.empty()) view["signals"] = std::move(signals);
    if (!c.models.empty()) {
        Json models = Json::object();
        for (const std::string& m : c.models) models[m] = "row";
        view["models"] = std::move(models);
    }
    view["root"] = std::move(r);
    return Json{{"omui", "0.1"}, {"view", std::move(view)}};
}

}  // namespace

Json Session::document(const Element& root) const {
    std::map<std::string, Handler> handlers;
    return compile(root, handlers);
}

Result<void> Session::mount(std::string slot, const Element& root) {
    std::map<std::string, Handler> handlers;
    const std::string text = dump(compile(root, handlers));

    struct Forward {
        Host*                 host;
        std::shared_ptr<bool> alive;
        Handler               handler;
    };
    std::vector<std::shared_ptr<void>> users;
    std::vector<abi::OmuiSubscription> subs;
    std::string diagnostics;
    const abi::OmuiResult result = host_.ui([&] {
        auto rt = host_.runtime();
        if (auto it = mounted_.find(slot); it != mounted_.end())
            for (abi::OmuiSubscription s : it->second.subscriptions) (void)abi::omuiOff(rt, s);
        abi::OmuiResult x = abi::omuiMount(rt, str(slot), str(text));
        if (x != abi::OMUI_SUCCESS) {
            (void)abi::omuiCheck(rt, str(text), collect, &diagnostics);
            return x;
        }
        abi::OmuiNode node = 0;
        if ((x = abi::omuiFind(rt, str(slot), &node)) != abi::OMUI_SUCCESS) return x;
        for (const auto& [event, h] : handlers) {
            auto f = std::make_shared<Forward>(Forward{&host_, alive_, h});
            abi::OmuiSubscription sub = 0;
            x = abi::omuiOn(
                rt, node, str(event),
                [](void* user, abi::OmuiNode, abi::OmuiString, abi::OmuiString args) {
                    auto* f = static_cast<Forward*>(user);
                    Json a = Json::parse(std::string_view(args.data, args.size), nullptr, false);
                    Json e = a.is_object() && a.contains("event") ? a["event"] : Json(nullptr);
                    if (e.is_null()) e = Json::object();
                    if (a.is_object() && a.contains("rows") && e.is_object()) e["rows"] = a["rows"];
                    std::weak_ptr<bool> alive = f->alive;
                    f->host->post([alive, h = f->handler, e = std::move(e)] {
                        if (auto live = alive.lock(); live && *live) h(e);
                    });
                },
                f.get(), &sub);
            if (x != abi::OMUI_SUCCESS) return x;
            subs.push_back(sub);
            users.push_back(std::move(f));
        }
        return abi::OMUI_SUCCESS;
    });
    if (result != abi::OMUI_SUCCESS) return std::unexpected(failure(result, diagnostics));
    Mounted& m = mounted_[slot];  // the old subscriptions are off: their users can go
    m.subscriptions = std::move(subs);
    m.handlers = std::move(handlers);
    m.users = std::move(users);
    return {};
}

Result<void> Session::unmount(std::string slot) {
    auto it = mounted_.find(slot);
    std::vector<abi::OmuiSubscription> subs = it == mounted_.end() ? std::vector<abi::OmuiSubscription>{} : it->second.subscriptions;
    const abi::OmuiResult r = host_.ui([&] {
        for (abi::OmuiSubscription s : subs) (void)abi::omuiOff(host_.runtime(), s);
        return abi::omuiUnmount(host_.runtime(), str(slot));
    });
    if (it != mounted_.end()) mounted_.erase(it);
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r, std::format("nothing is mounted at `{}`", slot)));
    return {};
}

Result<void> Session::splice(std::string_view model, std::size_t from, std::size_t remove, const Json& rows) {
    const std::string text = dump(rows);
    const abi::OmuiResult r = host_.ui([&] {
        return abi::omuiModelSplice(host_.runtime(), str(model), static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(remove), str(text));
    });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r, std::format("the rows of `{}` were refused (keys are unique ids)", model)));
    return {};
}

Result<Json> Session::rows(std::string_view model, std::size_t from, std::size_t count) {
    std::string out;
    const abi::OmuiResult r = host_.ui([&] {
        return abi::omuiModelRows(host_.runtime(), str(model), static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(count), collect, &out);
    });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r));
    return Json::parse(out, nullptr, false);
}

Result<abi::OmuiNode> Session::find(std::string_view selector) {
    abi::OmuiNode n = 0;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiFind(host_.runtime(), str(selector), &n); });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r, std::format("no node `{}`", selector)));
    return n;
}

Result<Json> Session::get(abi::OmuiNode node, std::string_view prop) {
    std::string out;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiGet(host_.runtime(), node, str(prop), collect, &out); });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r));
    return Json::parse(out, nullptr, false);
}

Result<void> Session::set(abi::OmuiNode node, const std::vector<std::pair<std::string, Json>>& props) {
    std::vector<std::string> names, values;
    for (const auto& [k, v] : props) {
        names.push_back(k);
        values.push_back(dump(v));
    }
    const abi::OmuiResult r = host_.ui([&] {
        std::vector<abi::OmuiProp> batch(props.size());
        for (std::size_t i = 0; i < props.size(); ++i) batch[i] = {str(names[i]), json_value(values[i])};
        return abi::omuiSet(host_.runtime(), node, batch.data(), static_cast<std::uint32_t>(batch.size()));
    });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r));
    return {};
}

// ── themes ─────────────────────────────────────────────────────────────────

Result<void> Session::theme(const std::vector<std::string>& layers) {
    const std::string l = dump(Json(layers));
    std::string out;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiTheme(host_.runtime(), str(l), collect, &out); });
    for (const Json& rec : records(out))
        if (rec.value("kind", "") == "error") return std::unexpected(failure(r, rec.value("code", "error") + ": " + rec.value("text", "")));
    if (r < 0) return std::unexpected(failure(r, out));
    return {};
}

Json Session::themes() {
    std::string out;
    (void)host_.ui([&] { return abi::omuiThemes(host_.runtime(), collect, &out); });
    Json t = Json::parse(out, nullptr, false);
    return t.is_discarded() ? Json::object() : t;
}

// ── native modules ────────────────────────────────────────────────────────

Json Session::natives() {
    std::string out;
    (void)abi::omuiNativeCatalog(host_.runtime(), collect, &out);
    Json c = Json::parse(out, nullptr, false);
    return c.is_discarded() ? Json{{"modules", Json::object()}} : c;
}

Result<Json> Session::native(std::string_view module, std::string_view function, const Json& args) {
    const std::string a = dump(args);
    std::string out;
    const abi::OmuiResult r = abi::omuiNativeCall(host_.runtime(), str(module), str(function), str(a), collect, &out);
    Json data = nullptr;
    for (const Json& rec : records(out)) {
        if (rec.value("kind", "") == "error") return std::unexpected(failure(r, rec.value("code", "error") + ": " + rec.value("text", "")));
        if (rec.value("kind", "") == "data" && rec.contains("data")) data = rec["data"];
    }
    if (r < 0) return std::unexpected(failure(r, out));
    return data;
}

void Session::native_async(std::string module, std::string function, Json args, std::function<void(Result<Json>)> done) {
    if (!workers_) workers_ = std::make_unique<Workers>(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
    in_flight_->fetch_add(1);
    workers_->push([this, alive = std::weak_ptr<bool>(alive_), in_flight = in_flight_, module = std::move(module), function = std::move(function),
                    args = std::move(args), done = std::move(done)]() mutable {
        // The session outlives this: its destructor joins the workers.
        Result<Json> r = native(module, function, args);
        host_.post([alive, in_flight, done = std::move(done), r = std::move(r)]() mutable {
            if (auto live = alive.lock(); live && *live) done(std::move(r));
            in_flight->fetch_sub(1);
        });
    });
}

Result<std::vector<Json>> Session::snapshot(const Json& query) {
    const std::string q = dump(query);
    std::string out;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiSnapshot(host_.runtime(), str(q), collect, &out); });
    if (r < 0 && out.empty()) return std::unexpected(failure(r, out));
    return records(out);
}

Result<std::vector<Json>> Session::act(const Json& request) {
    const std::string q = dump(request);
    std::string out;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiAct(host_.runtime(), str(q), collect, &out); });
    // A refusal is an answer (its error record says why: not-found,
    // invalid-argument, needs-answer …); only a call that got none failed.
    if (r < 0 && out.empty()) return std::unexpected(failure(r, out));
    return records(out);
}

Result<std::vector<Json>> Session::invoke(std::string_view verb, const Json& args) {
    const std::string a = dump(args);
    std::string out;
    const abi::OmuiResult r = host_.ui([&] { return abi::omuiInvoke(host_.runtime(), str(verb), str(a), collect, &out); });
    if (r < 0 && out.empty()) return std::unexpected(failure(r, out));
    return records(out);
}

Result<void> Session::verb(VerbSpec spec, std::function<Result<Json>(const Json& args)> fn) {
    // Registered by this session before (a script run again, hot reload): the
    // new function answers from now on, under the same description.
    for (auto& b : verbs_)
        if (b->spec.name == spec.name) {
            if (b->spec.effect != spec.effect || b->spec.answer != spec.answer || b->spec.person != spec.person)
                return std::unexpected(failure(abi::OMUI_ERROR_REFUSED, std::format("the verb `{}` is registered already, with another effect or answer", spec.name)));
            b->fn = std::move(fn);  // on the script thread, where it is called
            b->spec.summary = spec.summary;
            return {};
        }
    auto box = std::make_unique<VerbBox>(VerbBox{&host_, std::move(fn), spec});
    const abi::OmuiResult r = host_.ui([&] {
        abi::OmuiVerbDesc d{};
        d.type = abi::OMUI_STRUCTURE_TYPE_VERB_DESC;
        d.name = str(spec.name);
        d.summary = str(spec.summary);
        d.effect = spec.effect;
        d.answer = str(spec.answer);
        abi::OmuiVerbPerson person{};
        person.type = abi::OMUI_STRUCTURE_TYPE_VERB_PERSON;
        person.person = 1;
        if (spec.person) d.next = &person;
        return abi::omuiRegisterVerb(
            host_.runtime(), &d,
            [](void* user, abi::OmuiString args, abi::PFN_omuiSink sink, void* sink_user) -> abi::OmuiResult {
                auto* b = static_cast<VerbBox*>(user);
                const Json a = Json::parse(std::string_view(args.data, args.size), nullptr, false);
                // The runtime waits for the script, and answers it meanwhile.
                Result<Json> out = b->host->script([&] { return b->fn(a); });
                const std::string text = out ? dump(*out) : out.error().text();
                sink(sink_user, text.data(), text.size());
                return out ? abi::OMUI_SUCCESS : abi::OMUI_ERROR_REFUSED;
            },
            box.get());
    });
    if (r != abi::OMUI_SUCCESS) return std::unexpected(failure(r, std::format("the verb `{}` was refused", spec.name)));
    verbs_.push_back(std::move(box));
    return {};
}

}  // namespace omui::lang
