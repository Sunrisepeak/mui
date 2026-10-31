module openmui.agent;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;
import openmui.trace;
import openmui.pipeline;

namespace omui::agent {

namespace {

constexpr std::array<std::string_view, 20> operations{"describe", "snapshot", "changes", "find",     "act",   "invoke",  "undo",
                                                       "redo",     "transaction", "history", "wait", "capture", "input", "inspect",
                                                       "stats",    "profile",  "logs",     "debug",   "pipeline", "clock"};

// A stage's configuration from a request's JSON object.
pipeline::Config config_of(const Value* v) {
    pipeline::Config c;
    if (!v || !v->map()) return c;
    for (const auto& [k, x] : *v->map()) {
        switch (x.type()) {
            case Value::Type::boolean: c.set(k, x.as_bool()); break;
            case Value::Type::integer: c.set(k, x.as_int()); break;
            case Value::Type::real: c.set(k, x.as_double()); break;
            case Value::Type::text: c.set(k, std::string(x.as_string())); break;
            default: break;
        }
    }
    return c;
}

Value value_of(const pipeline::ConfigValue& v) {
    return std::visit([](const auto& x) { return Value(x); }, v);
}

// What `pipeline` answers: the stages in order with their plugins, the last
// frame's time per stage, the plugins there are, and the description as TOML.
Value pipeline_value(Runtime& rt) {
    pipeline::Pipeline& p = rt.frame_pipeline();
    const pipeline::Description& d = p.description();
    Value v;
    v.set("name", d.name);
    Value::List stages;
    for (const pipeline::StageTime& t : p.timings()) {
        Value s;
        s.set("name", t.name).set("ms", t.ms);
        if (auto it = std::ranges::find(d.stages, t.name, &pipeline::StageDesc::name); it != d.stages.end()) {
            s.set("plugin", it->plugin);
            Value cfg = Value(Value::Map{});
            for (const auto& [k, x] : it->config.entries()) cfg.set(k, value_of(x));
            s.set("config", std::move(cfg));
        }
        stages.push_back(std::move(s));
    }
    v.set("stages", Value(std::move(stages)));
    Value::List plugins;
    for (const pipeline::PluginInfo& info : rt.stage_plugins().plugins()) {
        Value x;
        x.set("name", info.name).set("kind", info.kind).set("description", info.description);
        plugins.push_back(std::move(x));
    }
    v.set("plugins", Value(std::move(plugins)));
    v.set("graph", p.describe()).set("toml", pipeline::to_toml(d));
    return v;
}

Envelope invalid(std::string message, std::string_view hint = {}) {
    Envelope e;
    Value extra;
    if (!hint.empty()) extra.set("hint", hint);
    e.error(spec::error_code::invalid_argument, message, std::move(extra)).result(spec::exit_code::failure);
    return e;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    static constexpr std::string_view table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t{bytes[i]} << 16) | (std::uint32_t{bytes[i + 1]} << 8) | bytes[i + 2];
        for (int s : {18, 12, 6, 0}) out += table[(v >> s) & 63];
    }
    if (const std::size_t rest = bytes.size() - i; rest > 0) {
        std::uint32_t v = std::uint32_t{bytes[i]} << 16;
        if (rest == 2) v |= std::uint32_t{bytes[i + 1]} << 8;
        out += table[(v >> 18) & 63];
        out += table[(v >> 12) & 63];
        out += rest == 2 ? table[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string_view text_of(const Value& req, std::string_view key) {
    const Value* v = req.get(key);
    return v ? v->as_string() : std::string_view{};
}

Value history_value(const History& h) {
    auto items = [](const std::vector<HistoryItem>& from) {
        Value::List out;
        for (const HistoryItem& i : from) {
            Value x;
            x.set("label", i.label).set("steps", static_cast<std::int64_t>(i.steps));
            if (i.barrier) x.set("barrier", true);
            out.push_back(std::move(x));
        }
        return Value(std::move(out));
    };
    Value v;
    v.set("scope", h.scope).set("undo", items(h.undo)).set("redo", items(h.redo));
    return v;
}


constexpr std::string_view input_example =
    R"({"op":"input","events":[{"type":"click","node":"main/save"},{"type":"click","x":120,"y":48},{"type":"text","text":"hi"},)"
    R"({"type":"key","key":"Enter"},{"type":"drag","from":[10,10],"to":[200,10]},{"type":"wheel","x":300,"y":200,"dy":-120},{"type":"wait","frames":30}]})";

std::optional<MouseButton> button_of(std::string_view b) {
    if (b.empty() || b == "left") return MouseButton::left;
    if (b == "right") return MouseButton::right;
    if (b == "middle") return MouseButton::middle;
    return std::nullopt;
}

std::uint32_t held_bit(MouseButton b) {
    switch (b) {
        case MouseButton::left: return 1;
        case MouseButton::right: return 2;
        case MouseButton::middle: return 4;
        default: return 0;
    }
}

// The frames a list of input events takes, one batch a frame, as a
// person's devices would give them: a click is a move, a press and, a frame
// later, a release; a drag presses, moves in steps and releases; a key is
// pressed and released a frame later (or after `hold` frames).
std::expected<std::deque<std::vector<InputEvent>>, std::string> frames_of(const Value& events, Runtime& rt) {
    if (!events.list() || events.list()->empty()) return std::unexpected(std::string("`input` needs `events`: a list of events"));
    std::deque<std::vector<InputEvent>> out;
    Point at{};
    auto number = [](const Value& e, std::string_view k, double fallback) {
        const Value* v = e.get(k);
        return v && v->is_number() ? v->as_double() : fallback;
    };
    // A point: [x, y], or the centre of a node's bounds as it is laid out now.
    auto point = [&](const Value& e, std::string_view xy, std::string_view node) -> std::expected<Point, std::string> {
        if (const Value* v = e.get(node); v && !v->as_string().empty()) {
            const NodeId n = rt.find(v->as_string());
            if (!rt.valid(n)) return std::unexpected(std::format("no node '{}'", v->as_string()));
            const Rect b = rt.screen_bounds(n);
            if (b.empty()) return std::unexpected(std::format("'{}' has no bounds (not laid out, or not shown)", v->as_string()));
            return b.center();
        }
        if (const Value* v = e.get(xy); v && v->list() && v->list()->size() == 2)
            return Point{static_cast<float>((*v->list())[0].as_double()), static_cast<float>((*v->list())[1].as_double())};
        if (e.get("x") && e.get("y")) return Point{static_cast<float>(number(e, "x", 0)), static_cast<float>(number(e, "y", 0))};
        return std::unexpected(std::string("an event names where: `x` and `y` (logical pixels), or `node` (its centre)"));
    };
    auto mods_of = [](const Value& e) -> std::expected<Modifiers, std::string> {
        Modifiers m;
        if (const Value* v = e.get("mods"); v && v->list())
            for (const Value& x : *v->list()) {
                const std::string_view n = x.as_string();
                if (n == "shift") m = m | Modifier::shift;
                else if (n == "ctrl" || n == "control") m = m | Modifier::ctrl;
                else if (n == "alt") m = m | Modifier::alt;
                else if (n == "meta" || n == "cmd") m = m | Modifier::meta;
                else return std::unexpected(std::format("`{}` is not a modifier (shift, ctrl, alt, meta)", n));
            }
        return m;
    };
    auto pointer = [&](PointerKind kind, Point p, MouseButton b, std::uint32_t held, Modifiers m, std::uint8_t clicks = 1) {
        PointerEvent e;
        e.kind = kind;
        e.position = p;
        e.button = kind == PointerKind::move ? MouseButton::none : b;
        e.buttons = held;
        e.mods = m;
        e.clicks = clicks;
        return InputEvent{e};
    };
    for (const Value& e : *events.list()) {
        const std::string_view type = text_of(e, "type");
        auto mods = mods_of(e);
        if (!mods) return std::unexpected(mods.error());
        auto button = button_of(text_of(e, "button"));
        if (!button) return std::unexpected(std::format("`{}` is not a button (left, right, middle)", text_of(e, "button")));
        const std::uint32_t bit = held_bit(*button);
        if (type == "move" || type == "click" || type == "double_click" || type == "down" || type == "up" || type == "wheel") {
            if (type != "up" || e.get("x") || e.get("node")) {
                auto p = point(e, "at", "node");
                if (!p) return std::unexpected(std::format("{}: {}", type, p.error()));
                at = *p;
            }
        }
        if (type == "move") {
            out.push_back({pointer(PointerKind::move, at, *button, 0, *mods)});
        } else if (type == "click" || type == "double_click") {
            out.push_back({pointer(PointerKind::move, at, *button, 0, *mods), pointer(PointerKind::down, at, *button, bit, *mods)});
            out.push_back({pointer(PointerKind::up, at, *button, 0, *mods)});
            if (type == "double_click") {
                out.push_back({pointer(PointerKind::down, at, *button, bit, *mods, 2)});
                out.push_back({pointer(PointerKind::up, at, *button, 0, *mods, 2)});
            }
        } else if (type == "down") {
            out.push_back({pointer(PointerKind::move, at, *button, 0, *mods), pointer(PointerKind::down, at, *button, bit, *mods)});
        } else if (type == "up") {
            out.push_back({pointer(PointerKind::up, at, *button, 0, *mods)});
        } else if (type == "drag") {
            auto from = point(e, "from", "from_node");
            auto to = point(e, "to", "to_node");
            if (!from || !to) return std::unexpected(std::format("drag: {} (`from` and `to`: [x, y], or `from_node` and `to_node`)",
                                                                 !from ? from.error() : to.error()));
            const int steps = std::clamp(static_cast<int>(number(e, "steps", 12)), 1, 600);
            out.push_back({pointer(PointerKind::move, *from, *button, 0, *mods), pointer(PointerKind::down, *from, *button, bit, *mods)});
            for (int i = 1; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                out.push_back({pointer(PointerKind::move, {from->x + (to->x - from->x) * t, from->y + (to->y - from->y) * t}, *button, bit, *mods)});
            }
            out.push_back({pointer(PointerKind::up, *to, *button, 0, *mods)});
            at = *to;
        } else if (type == "wheel") {
            WheelEvent w;
            w.position = at;
            w.delta = {static_cast<float>(number(e, "dx", 0)), static_cast<float>(number(e, "dy", 0))};
            w.precise = e.get("precise") && e.get("precise")->as_bool();
            w.mods = *mods;
            out.push_back({pointer(PointerKind::move, at, *button, 0, *mods), InputEvent{w}});
        } else if (type == "key" || type == "key_down" || type == "key_up") {
            const auto key = parse_key(text_of(e, "key"));
            if (!key) return std::unexpected(std::format("`{}` is not a key (A, Enter, Escape, Tab, Space, Left, PageUp, F5 …)", text_of(e, "key")));
            KeyEvent k;
            k.key = *key;
            k.mods = *mods;
            if (type != "key_up") {
                k.kind = KeyKind::down;
                out.push_back({InputEvent{k}});
            }
            if (type == "key") {
                const int hold = std::clamp(static_cast<int>(number(e, "hold", 1)), 1, 600);
                for (int i = 1; i < hold; ++i) out.emplace_back();
            }
            if (type != "key_down") {
                k.kind = KeyKind::up;
                out.push_back({InputEvent{k}});
            }
        } else if (type == "keys") {
            // Several keys pressed in the same frame, held, released together (a chord: LP+LK).
            const Value* list = e.get("keys");
            if (!list || !list->list() || list->list()->empty()) return std::unexpected(std::string("keys: needs `keys`, a list: [\"U\", \"J\"]"));
            std::vector<KeyEvent> held;
            for (const Value& k : *list->list()) {
                const auto key = parse_key(k.as_string());
                if (!key) return std::unexpected(std::format("`{}` is not a key", k.as_string()));
                held.push_back({.kind = KeyKind::down, .key = *key, .mods = *mods});
            }
            std::vector<InputEvent> down, up;
            for (KeyEvent k : held) {
                down.push_back(k);
                k.kind = KeyKind::up;
                up.push_back(k);
            }
            out.push_back(std::move(down));
            const int hold = std::clamp(static_cast<int>(number(e, "hold", 1)), 1, 600);
            for (int i = 1; i < hold; ++i) out.emplace_back();
            out.push_back(std::move(up));
        } else if (type == "text") {
            const std::string_view text = text_of(e, "text");
            if (text.empty()) return std::unexpected(std::string("text: needs `text`"));
            out.push_back({InputEvent{TextEvent{std::string(text)}}});
        } else if (type == "wait") {
            const int frames = std::clamp(static_cast<int>(number(e, "frames", 1)), 1, 3600);
            for (int i = 0; i < frames; ++i) out.emplace_back();
        } else {
            return std::unexpected(std::format("`{}` is not an input event (move, click, double_click, down, up, drag, wheel, key, keys, key_down, "
                                               "key_up, text, wait)",
                                               type));
        }
    }
    return out;
}

}  // namespace

std::vector<std::string> lines(const Envelope& envelope, const Value& re) {
    std::vector<std::string> out;
    for (const spec::Record& r : envelope.records()) {
        if (re.is_null()) {
            out.push_back(r.json());
            continue;
        }
        Value v;
        v.set("kind", spec::name(r.kind));
        if (const Value::Map* m = r.body.map())
            for (const auto& [k, x] : *m) v.set(k, x);
        v.set("re", re);
        out.push_back(v.json());
    }
    return out;
}

Protocol::Protocol(Runtime& runtime, ProtocolOptions options)
    : runtime_(runtime), tree_(runtime), options_(std::move(options)) {}

void Protocol::send(const Reply& reply, const Envelope& envelope, const Value& re) const {
    for (const std::string& l : lines(envelope, re)) reply(l);
}

void Protocol::request(std::string_view line, const Reply& reply) {
    auto req = parse_json(line);
    if (!req) {
        send(reply, invalid(std::format("the request is not JSON: {}", req.error().text()),
                            R"(one JSON object per line, e.g. {"op":"snapshot"})"),
             {});
        return;
    }
    if (!req->map()) {
        send(reply, invalid("a request is a JSON object", R"({"op":"snapshot"})"), {});
        return;
    }
    const Value re = req->get("re") ? *req->get("re") : Value{};
    bool deferred = false;
    Envelope e = handle(*req, re, reply, deferred);
    if (!deferred) send(reply, e, re);
}

Envelope Protocol::handle(const Value& req, const Value& re, const Reply& reply, bool& deferred) {
    const std::string_view op = text_of(req, "op");
    Envelope e;
    if (op == "describe") {
        Value::List ops;
        for (std::string_view o : operations) ops.emplace_back(o);
        Value d;
        d.set("interface", interface_name).set("spec", spec::version).set("app", options_.app).set("operations", Value(std::move(ops)));
        d.set("revision", static_cast<std::int64_t>(runtime_.semantic_revision()));
        d.set("capture", static_cast<bool>(options_.capture));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "snapshot") {
        semantic::SnapshotOptions o;
        o.root = std::string(text_of(req, "root"));
        if (const Value* v = req.get("depth")) o.depth = static_cast<int>(v->as_int(-1));
        if (const Value* v = req.get("interactive")) o.interactive = v->as_bool();
        if (const Value* v = req.get("offset")) o.offset = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        if (const Value* v = req.get("limit")) o.limit = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        if (!o.root.empty() && !tree_.node(o.root)) {
            e.error(spec::error_code::not_found, std::format("no node '{}'", o.root)).result(spec::exit_code::failure);
            return e;
        }
        return e.data(tree_.snapshot(o).to_value()).result(spec::exit_code::success);
    }
    if (op == "changes") {
        return e.data(spec::to_value(tree_.changes())).result(spec::exit_code::success);
    }
    if (op == "find") {
        const Value* q = req.get("q");
        if (!q) return invalid("`find` needs a query `q`", R"({"op":"find","q":{"role":"button","name":"Save*"}})");
        auto query = semantic::Query::parse(*q);
        if (!query) return invalid(query.error());
        Value::List nodes;
        for (const spec::SemanticNode& n : tree_.find(*query)) nodes.push_back(spec::to_value(n));
        return e.data(Value{}.set("nodes", Value(std::move(nodes)))).result(spec::exit_code::success);
    }
    // Who asks, with what: the actor, the answer, the arguments, a dry run.
    auto invocation = [&](const Value& from) -> std::expected<Invocation, Envelope> {
        Invocation call;
        if (const Value* a = from.get("args")) call.args = *a;
        call.answer = std::string(text_of(from, "answer"));
        if (call.answer.empty()) call.answer = std::string(text_of(req, "answer"));
        const std::string_view actor = text_of(req, "actor");
        auto parsed = Actor::parse(actor.empty() ? std::string_view(options_.default_actor) : actor);
        if (!parsed)
            return std::unexpected(invalid(std::format("`{}` is not an actor", actor), "kind:name[;via=kind:name], e.g. agent:planner;via=person:alice"));
        call.actor = std::move(*parsed);
        call.remote = options_.remote;  // over the bus: whatever actor it names (S-10)
        if (const Value* d = req.get("dry_run")) call.dry_run = d->as_bool();  // only what would happen (S-13)
        return call;
    };
    if (op == "act") {
        const std::string_view node = text_of(req, "node");
        const std::string_view action = text_of(req, "action");
        if (node.empty() || action.empty())
            return invalid("`act` needs `node` and `action`", R"({"op":"act","node":"main/save","action":"activate","dry_run":false})");
        auto call = invocation(req);
        if (!call) return call.error();
        return tree_.act(node, action, *call);
    }
    if (op == "invoke") {
        // An application verb (S-1 as for a person's: an irreversible or
        // external one needs its answer).
        const std::string_view verb = text_of(req, "verb");
        if (verb.empty()) return invalid("`invoke` needs a `verb`", R"({"op":"invoke","verb":"notes.new","args":{"title":"Today"}})");
        auto call = invocation(req);
        if (!call) return call.error();
        return runtime_.invoke(verb, *call);
    }
    if (op == "undo" || op == "redo") {
        // The history of a scope (S-12): the window's, or a node's with `undo_scope`.
        const std::string_view scope = text_of(req, "scope");
        auto call = invocation(req);
        if (!call) return call.error();
        call->args = {};
        return tree_.act(scope.empty() ? std::string_view("app") : scope, op, *call);
    }
    if (op == "history") {
        const std::string_view scope = text_of(req, "scope");
        NodeId n = scope.empty() ? runtime_.root() : runtime_.find(scope);
        if (!runtime_.valid(n)) {
            e.error(spec::error_code::not_found, std::format("no node '{}'", scope)).result(spec::exit_code::failure);
            return e;
        }
        // A node's own history is its scope's: the nearest around it.
        return e.data(history_value(runtime_.history(runtime_.undo_scope(n)))).result(spec::exit_code::success);
    }
    if (op == "transaction") {
        // Steps performed as one (S-13): all done, or all taken back.
        const Value* steps = req.get("steps");
        constexpr std::string_view example =
            R"({"op":"transaction","label":"rename","steps":[{"node":"main/title","action":"set_value","args":{"value":"Plan"}},{"node":"main/done","action":"toggle"}]})";
        if (!steps || !steps->list() || steps->list()->empty()) return invalid("`transaction` needs `steps`: a list of {node, action, args, answer}", example);
        std::vector<semantic::Step> list;
        for (const Value& st : *steps->list()) {
            if (!st.map() || text_of(st, "node").empty() || text_of(st, "action").empty()) return invalid("each step names its `node` and `action`", example);
            list.push_back({std::string(text_of(st, "node")), std::string(text_of(st, "action")), st.get("args") ? *st.get("args") : Value{},
                            std::string(text_of(st, "answer"))});
        }
        auto call = invocation(Value{});
        if (!call) return call.error();
        return tree_.transaction(list, *call, std::string(text_of(req, "label")));
    }
    if (op == "wait") {
        // The condition's fields sit in the request itself: q, absent, settled.
        Value fields = Value(Value::Map{});
        for (std::string_view k : {"q", "absent", "settled"})
            if (const Value* v = req.get(k)) fields.set(k, *v);
        auto condition = semantic::Condition::parse(fields);
        if (!condition) return invalid(condition.error(), R"({"op":"wait","q":{"id":"main/toast"},"settled":true,"timeout":5})");
        const double timeout = req.get("timeout") ? req.get("timeout")->as_double(options_.default_timeout) : options_.default_timeout;
        if (auto now = tree_.check(*condition); now.satisfied) {
            return e.data(Value{}.set("satisfied", true).set("revision", static_cast<std::int64_t>(tree_.revision())))
                       .result(spec::exit_code::success);
        }
        deferred = true;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::max(0.0, timeout)));
        waits_.push_back({std::move(*condition), re, deadline, reply});
        runtime_.request_frame();
        return e;
    }
    if (op == "capture") {
        if (!options_.capture) {
            e.error(spec::error_code::refused, "this host does not render (headless without a renderer)").result(spec::exit_code::failure);
            return e;
        }
        Rect region{0, 0, runtime_.size().width, runtime_.size().height};
        if (const std::string_view id = text_of(req, "node"); !id.empty()) {
            NodeId n = runtime_.find(id);
            if (!runtime_.valid(n)) {
                e.error(spec::error_code::not_found, std::format("no node '{}'", id)).result(spec::exit_code::failure);
                return e;
            }
            region = runtime_.screen_bounds(n);
        }
        // A part of it, to look closer (a hand, a joint): [x, y, width, height], from the node's corner (or the window's).
        if (const Value* r = req.get("region")) {
            const Value::List* l = r->list();
            if (!l || l->size() != 4 || !std::ranges::all_of(*l, [](const Value& v) { return v.is_number(); }))
                return invalid("`region` is [x, y, width, height] in logical pixels", R"({"op":"capture","region":[100,80,200,150]})");
            const Rect want{region.x + static_cast<float>((*l)[0].as_double()), region.y + static_cast<float>((*l)[1].as_double()),
                            static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
            const float x0 = std::max(want.x, region.x), y0 = std::max(want.y, region.y);
            const float x1 = std::min(want.x + want.width, region.x + region.width), y1 = std::min(want.y + want.height, region.y + region.height);
            if (!(x1 > x0 && y1 > y0)) return invalid("`region` lies outside what is captured", R"({"op":"capture","region":[100,80,200,150]})");
            region = {x0, y0, x1 - x0, y1 - y0};
        }
        auto png = options_.capture(region);
        if (!png) {
            e.error(spec::error_code::internal, png.error()).result(spec::exit_code::failure);
            return e;
        }
        Value d;
        d.set("format", "png").set("bounds", Value(Value::List{Value(region.x), Value(region.y), Value(region.width), Value(region.height)}));
        d.set("base64", base64(*png));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "input") {
        // A person's input, as their devices give it, delivered a frame at a
        // time; answered once the last of it has been through a frame, with
        // what each press hit and the actions it performed.
        if (!options_.input) {
            e.error(spec::error_code::refused,
                    "this application takes no input from agents (AppSpec::agent_input off, or --no-agent-input): act on its nodes instead")
                .result(spec::exit_code::failure);
            return e;
        }
        const Value* events = req.get("events");
        if (!events) return invalid("`input` needs `events`", input_example);
        auto frames = frames_of(*events, runtime_);
        if (!frames) return invalid(frames.error(), input_example);
        auto call = invocation(req);
        if (!call) return call.error();
        // What the input does is the agent's, through the input (openas §2.7).
        Actor by = call->actor;
        by.via.emplace_back("service:input");
        deferred = true;
        deliveries_.push_back({re, reply, std::move(by), std::move(*frames), 0, runtime_.action_journal().size(), {}});
        runtime_.request_frame();
        return e;
    }
    // ── development: inspection, statistics, profiling, logs, debug overlays ──
    if (op == "inspect") {
        const std::string_view id = text_of(req, "node");
        const NodeId n = id.empty() ? runtime_.root() : runtime_.reveal(id);
        if (!runtime_.valid(n)) {
            e.error(spec::error_code::not_found, std::format("no node '{}'", id)).result(spec::exit_code::failure);
            return e;
        }
        return e.data(runtime_.inspect(n)).result(spec::exit_code::success);
    }
    if (op == "stats") {
        const auto frames = runtime_.frame_history();
        Value d;
        d.set("frames", static_cast<std::int64_t>(frames.size()));
        if (!frames.empty()) {
            std::vector<double> totals;
            FrameTimings mean;
            for (const FrameStats& f : frames) {
                totals.push_back(f.timings.total);
                for (auto [m, x] : {std::pair{&mean.timers, f.timings.timers}, {&mean.input, f.timings.input}, {&mean.regions, f.timings.regions},
                                    {&mean.animations, f.timings.animations}, {&mean.layout, f.timings.layout},
                                    {&mean.bounds, f.timings.bounds}, {&mean.paint, f.timings.paint}, {&mean.total, f.timings.total}})
                    *m += x / static_cast<double>(frames.size());
            }
            std::ranges::sort(totals);
            auto pct = [&](double p) {
                const auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(totals.size())));
                return totals[std::clamp<std::size_t>(rank, 1, totals.size()) - 1];
            };
            d.set("mean_ms", mean.total).set("p50_ms", pct(50)).set("p95_ms", pct(95)).set("p99_ms", pct(99)).set("max_ms", totals.back());
            Value phases;
            phases.set("timers", mean.timers).set("input", mean.input).set("regions", mean.regions).set("animations", mean.animations);
            phases.set("layout", mean.layout).set("bounds", mean.bounds).set("paint", mean.paint);
            d.set("phases_mean_ms", std::move(phases));
            const FrameStats& last = frames.back();
            Value l;
            l.set("nodes", static_cast<std::int64_t>(last.nodes)).set("laid_out", static_cast<std::int64_t>(last.laid_out));
            l.set("painted", static_cast<std::int64_t>(last.painted)).set("bindings", static_cast<std::int64_t>(last.bindings));
            l.set("animating", static_cast<std::int64_t>(last.animating));
            d.set("last", std::move(l));
            // "history": N — the last N frames, oldest first: total and the phases (a frame-time graph).
            if (const Value* h = req.get("history"); h && h->is_number()) {
                const auto want = static_cast<std::size_t>(std::clamp<std::int64_t>(h->as_int(0), 0, 240));
                Value::List recent;
                for (std::size_t i = frames.size() - std::min(want, frames.size()); i < frames.size(); ++i) {
                    const FrameTimings& ft = frames[i].timings;
                    Value f;
                    f.set("total", ft.total).set("layout", ft.layout).set("paint", ft.paint).set("animations", ft.animations);
                    f.set("other", ft.timers + ft.input + ft.regions + ft.bounds + ft.other);
                    recent.push_back(std::move(f));
                }
                d.set("history", Value(std::move(recent)));
            }
        }
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "profile") {
        const std::string_view what = text_of(req, "action");
        if (what == "start") {
            trace::start();
            return e.data(Value{}.set("recording", true)).result(spec::exit_code::success);
        }
        if (what == "stop") {
            const trace::Trace t = trace::stop();
            Value::List stats;
            for (const trace::Stat& st : t.summary()) {
                Value x;
                x.set("name", st.name).set("category", st.category).set("count", static_cast<std::int64_t>(st.count));
                x.set("total_ms", st.total_ms).set("mean_ms", st.mean_ms).set("p50_ms", st.p50_ms).set("p95_ms", st.p95_ms);
                x.set("p99_ms", st.p99_ms).set("max_ms", st.max_ms);
                stats.push_back(std::move(x));
            }
            Value d;
            d.set("summary", Value(std::move(stats))).set("events", static_cast<std::int64_t>(t.events.size()));
            d.set("dropped", static_cast<std::int64_t>(t.dropped));
            if (const Value* c = req.get("chrome"); c && c->as_bool()) d.set("chrome", t.chrome_json());
            return e.data(std::move(d)).result(spec::exit_code::success);
        }
        return invalid("`profile` takes action start or stop", R"({"op":"profile","action":"stop","chrome":true})");
    }
    if (op == "logs") {
        Level at_least = Level::trace;
        if (const std::string_view lv = text_of(req, "level"); !lv.empty()) {
            bool known = false;
            for (Level l : {Level::trace, Level::debug, Level::info, Level::warning, Level::error})
                if (name(l) == lv) {
                    at_least = l;
                    known = true;
                }
            if (!known) return invalid(std::format("`{}` is not a level", lv), "trace, debug, info, warning, error");
        }
        const auto limit = static_cast<std::size_t>(std::max<std::int64_t>(1, req.get("limit") ? req.get("limit")->as_int(100) : 100));
        const std::string_view category = text_of(req, "category");
        Value::List records;
        for (const LoggedRecord& r : recent_logs(10'000, at_least)) {
            if (!category.empty() && r.category != category) continue;
            records.push_back(parse_json(to_json(r)).value_or(Value{}));
        }
        if (records.size() > limit) records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(limit));
        return e.data(Value{}.set("records", Value(std::move(records)))).result(spec::exit_code::success);
    }
    if (op == "clock") {
        // The application's time (F4): pause, step, slow down, seek — the
        // animation inspector and time-travel debugging drive these.
        const std::string_view action = text_of(req, "action");
        auto number = [&](std::string_view key) -> std::optional<double> {
            const Value* v = req.get(key);
            if (!v || !v->is_number()) return std::nullopt;
            return v->as_double();
        };
        if (action.empty() || action == "show") {
        } else if (action == "pause") {
            runtime_.pause_time(true);
        } else if (action == "resume") {
            runtime_.pause_time(false);
        } else if (action == "rate") {
            const auto r = number("rate");
            if (!r || *r < 0) return invalid("`clock` rate needs a \"rate\" ≥ 0", R"({"op":"clock","action":"rate","rate":0.25})");
            runtime_.set_time_rate(*r);
        } else if (action == "step") {
            const auto dt = number("seconds");
            if (!dt) return invalid("`clock` step needs \"seconds\"", R"({"op":"clock","action":"step","seconds":0.016})");
            runtime_.step_time(seconds(*dt));
        } else if (action == "seek") {
            const auto t = number("seconds");
            if (!t) return invalid("`clock` seek needs \"seconds\"", R"({"op":"clock","action":"seek","seconds":1.5})");
            runtime_.seek_time(seconds(*t));
        } else if (action == "release") {
            runtime_.release_time();
        } else {
            return invalid(std::format("`clock` has no action `{}`", action), "actions: show, pause, resume, rate, step, seek, release");
        }
        runtime_.request_frame();
        const Runtime::TimeState ts = runtime_.time_state();
        Value d;
        d.set("host", ts.host.count()).set("now", ts.shown.count()).set("controlled", ts.controlled);
        d.set("paused", ts.paused).set("rate", ts.rate);
        d.set("pending", action == "step" || action == "seek");  // shown at the next frame
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "pipeline") {
        // The frame's pipeline (D-66): read it, or change one stage between frames.
        const std::string_view action = text_of(req, "action");
        std::expected<void, std::vector<pipeline::Problem>> changed;
        if (action.empty() || action == "show") {
        } else if (action == "replace") {
            changed = runtime_.frame_pipeline().replace(text_of(req, "stage"), text_of(req, "plugin"), config_of(req.get("config")),
                                                         runtime_.stage_plugins());
        } else if (action == "remove") {
            changed = runtime_.frame_pipeline().remove(text_of(req, "stage"), runtime_.stage_plugins());
        } else if (action == "insert") {
            pipeline::StageDesc s;
            s.name = std::string(text_of(req, "stage"));
            s.plugin = std::string(text_of(req, "plugin"));
            if (const Value* after = req.get("after"); after && after->list())
                for (const Value& a : *after->list()) s.after.emplace_back(a.as_string());
            s.config = config_of(req.get("config"));
            changed = runtime_.frame_pipeline().insert(std::move(s), runtime_.stage_plugins());
        } else {
            return invalid(std::format("`pipeline` has no action `{}`", action),
                           R"({"op":"pipeline","action":"replace","stage":"paint","plugin":"openmui.view.paint"} — actions: show, replace, insert, remove)");
        }
        if (!changed) {
            e.error(spec::error_code::invalid_argument, pipeline::to_text(changed.error())).result(spec::exit_code::failure);
            return e;
        }
        runtime_.request_frame();
        return e.data(pipeline_value(runtime_)).result(spec::exit_code::success);
    }
    if (op == "debug") {
        DebugOptions o = runtime_.debug();
        for (auto [k, field] : {std::pair{"bounds", &o.bounds}, {"repaints", &o.repaints}, {"inspector", &o.inspector}, {"hud", &o.hud}})
            if (const Value* v = req.get(k)) *field = v->as_bool();
        if (const Value* v = req.get("slow_frame_ms")) o.slow_frame_ms = v->as_double();
        if (const Value* v = req.get("log")) {
            if (auto r = set_log_filter(v->as_string()); !r) return invalid(r.error(), R"({"op":"debug","log":"layout=debug,*=info"})");
        }
        runtime_.set_debug(o);
        Value d;
        d.set("bounds", o.bounds).set("repaints", o.repaints).set("inspector", o.inspector).set("hud", o.hud);
        d.set("slow_frame_ms", o.slow_frame_ms);
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    std::string known;
    for (std::string_view o : operations) known += known.empty() ? std::string(o) : std::format(", {}", o);
    return invalid(op.empty() ? std::string("a request names its operation in `op`") : std::format("no operation `{}`", op),
                   std::format("operations: {}", known));
}

std::optional<AgentInput> Protocol::take_input() {
    for (Delivery& d : deliveries_) {
        if (d.frames.empty()) continue;
        std::vector<InputEvent> batch = std::move(d.frames.front());
        d.frames.pop_front();
        ++d.delivered;
        // What each press lands on, as the frame before it laid things out:
        // the node there, and the nearest around it the semantic tree states.
        for (const InputEvent& ev : batch) {
            const auto* p = std::get_if<PointerEvent>(&ev);
            if (!p || p->kind != PointerKind::down) continue;
            Value hit;
            hit.set("at", Value(Value::List{Value(static_cast<double>(p->position.x)), Value(static_cast<double>(p->position.y))}));
            const NodeId n = runtime_.hit_test(p->position);
            if (runtime_.valid(n)) {
                std::string path = runtime_.path(n);
                hit.set("node", path);
                for (std::string id = path; !id.empty();) {
                    if (auto sn = tree_.node(id)) {
                        hit.set("semantic", sn->id).set("role", spec::name(sn->role));
                        if (!sn->name.empty()) hit.set("name", sn->name);
                        break;
                    }
                    const auto slash = id.rfind('/');
                    id = slash == std::string::npos ? std::string() : id.substr(0, slash);
                }
            }
            d.pressed.push_back(std::move(hit));
        }
        runtime_.request_frame();
        return AgentInput{std::move(batch), d.actor};
    }
    return std::nullopt;
}

void Protocol::poll() {
    // Input whose last frame has run: what it did.
    while (!deliveries_.empty() && deliveries_.front().frames.empty() && deliveries_.front().delivered > 0) {
        Delivery& d = deliveries_.front();
        const auto journal = runtime_.action_journal();
        Value::List actions;
        for (std::size_t i = std::min(d.journal, journal.size()); i < journal.size(); ++i) {
            Value a;
            a.set("node", journal[i].path).set("action", journal[i].action).set("actor", journal[i].actor);
            a.set("exitCode", static_cast<std::int64_t>(journal[i].exit_code));
            actions.push_back(std::move(a));
        }
        Value data;
        data.set("frames", static_cast<std::int64_t>(d.delivered)).set("pressed", Value(std::move(d.pressed)));
        data.set("actions", Value(std::move(actions)));
        const NodeId f = runtime_.focused();
        data.set("focused", runtime_.valid(f) ? Value(runtime_.path(f)) : Value{});
        Envelope e;
        e.data(std::move(data)).result(spec::exit_code::success);
        send(d.reply, e, d.re);
        deliveries_.pop_front();
    }
    if (!deliveries_.empty()) runtime_.request_frame();
    if (waits_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<Wait> still;
    for (Wait& w : waits_) {
        const semantic::WaitState s = tree_.check(w.condition);
        Envelope e;
        if (s.satisfied) {
            e.data(Value{}.set("satisfied", true).set("revision", static_cast<std::int64_t>(tree_.revision())))
                .result(spec::exit_code::success);
        } else if (now >= w.deadline) {
            e.error(spec::error_code::timeout, std::format("the condition did not hold in time: {}", s.why))
                .result(spec::exit_code::not_ready);
        } else {
            still.push_back(std::move(w));
            continue;
        }
        send(w.reply, e, w.re);
    }
    waits_ = std::move(still);
    if (!waits_.empty()) runtime_.request_frame();
}

}  // namespace omui::agent
