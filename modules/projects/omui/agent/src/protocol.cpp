module omui.agent;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import mlibs.trace;
import mlibs.pipeline;
import mlibs.control;
import mlibs.base64;
import mlibs.image;
import mlibs.hash;
import xrender.vector;
import xrender.vector.svg;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
using mlibs::log::LoggedRecord;
using mlibs::log::recent_logs;
using mlibs::log::set_log_filter;
using mlibs::log::Level;
}

namespace omui::agent {

namespace {

constexpr std::size_t default_children_limit = 256;
constexpr Log protocol_log{"agent"};


// The protocol's operations are its catalog's (omui.ops.omui.ts), named on the
// wire without their `ui.` domain.
constexpr std::string_view wire_domain = "ui.";
constexpr std::string_view wire_name(const mlibs::control::CatalogEntry& entry) { return entry.operation.name.substr(wire_domain.size()); }
static_assert(std::ranges::all_of(catalog, [](const auto& entry) { return entry.operation.name.starts_with(wire_domain); }),
              "every operation of the protocol is in the ui domain");
const mlibs::control::CatalogEntry* entry_of(std::string_view op) {
    const auto it = std::ranges::find(catalog, op, wire_name);
    return it == catalog.end() ? nullptr : &*it;
}

// Policy classifies protocol controls too: an operation that runs what it names
// (act, invoke) is admitted as a read here and checked again by the Runtime
// against the action's or verb's declared effect; showing the clock or the
// pipeline reads. No caller-supplied mode, cap, grant or presence field
// participates in authorization.
Effect operation_effect(const mlibs::control::CatalogEntry& entry, const Value& req) {
    if (entry.delegated) return Effect::read;
    const auto op = wire_name(entry);
    const auto* action = req.get("action");
    if ((op == "clock" || op == "pipeline") && (!action || action->as_string().empty() || action->as_string() == "show"))
        return Effect::read;
    switch (entry.operation.effect) {
    case openas::Effect::read: return Effect::read;
    case openas::Effect::reversible: return Effect::reversible;
    case openas::Effect::irreversible: return Effect::irreversible;
    default: return Effect::external;
    }
}

// A stage's configuration from a request's JSON object.
mlibs::pipeline::Config config_of(const Value* v) {
    mlibs::pipeline::Config c;
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

Value value_of(const mlibs::pipeline::ConfigValue& v) {
    return std::visit([](const auto& x) { return Value(x); }, v);
}

// What `pipeline` answers: the stages in order with their plugins, the last
// frame's time per stage, the plugins there are, and the description as TOML.
Value pipeline_value(Runtime& rt) {
    mlibs::pipeline::Pipeline& p = rt.frame_pipeline();
    const mlibs::pipeline::Description& d = p.description();
    Value v;
    v.set("name", d.name);
    Value::List stages;
    for (const mlibs::pipeline::StageTime& t : p.timings()) {
        Value s;
        s.set("name", t.name).set("ms", t.ms);
        if (auto it = std::ranges::find(d.stages, t.name, &mlibs::pipeline::StageDesc::name); it != d.stages.end()) {
            s.set("plugin", it->plugin);
            Value cfg = Value(Value::Map{});
            for (const auto& [k, x] : it->config.entries()) cfg.set(k, value_of(x));
            s.set("config", std::move(cfg));
        }
        stages.push_back(std::move(s));
    }
    v.set("stages", Value(std::move(stages)));
    Value::List plugins;
    for (const mlibs::pipeline::PluginInfo& info : rt.stage_plugins().plugins()) {
        Value x;
        x.set("name", info.name).set("kind", info.kind).set("description", info.description);
        plugins.push_back(std::move(x));
    }
    v.set("plugins", Value(std::move(plugins)));
    v.set("graph", p.describe()).set("toml", mlibs::pipeline::to_toml(d));
    return v;
}

Envelope invalid(std::string message, std::string_view hint = {}) {
    Envelope e;
    Value extra;
    if (!hint.empty()) extra.set("hint", hint);
    e.error(spec::error_code::invalid_argument, message, std::move(extra)).result(spec::exit_code::failure);
    return e;
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
    // A point: [x, y], or the centre of the node as currently presented.
    // Target layout can be far ahead of a moving control. drawn_bounds also
    // retains the layout-only fallback for hosts that do not paint.
    auto point = [&](const Value& e, std::string_view xy, std::string_view node) -> std::expected<Point, std::string> {
        if (const Value* v = e.get(node); v && !v->as_string().empty()) {
            const NodeId n = rt.find(v->as_string());
            if (!rt.valid(n)) return std::unexpected(std::format("no node '{}'", v->as_string()));
            const Rect b = rt.drawn_bounds(n);
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

namespace {

// `report: "changes"` (an agent that would otherwise snapshot again to see
// what its step did, W4): the step's answer and, done, the semantic changes
// it made — added, removed, changed node by node — in one round trip.
template <class Step>
Envelope with_changes(Runtime& runtime, semantic::Tree& tree, const Value& req, const Invocation& call, Step step) {
    if (text_of(req, "report") != "changes" || call.dry_run) return step();
    const auto before = tree.snapshot();
    Envelope e = step();
    if (e.exit_code().value_or(1) != spec::exit_code::success) return e;
    // A frame at this very time (as a preview's): what the step made, its
    // reactive updates applied and laid out, before it is read.
    FrameInput still;
    still.time = runtime.now();
    (void)runtime.frame(still);
    // The result stays the last record: its data first, then the changes, then the result
    // (an answer of another shape — notices, logs — goes as it is).
    Envelope out;
    for (const spec::Record& r : e.records()) {
        if (r.kind == spec::RecordKind::result) break;
        if (r.kind != spec::RecordKind::data || !r.body.get("data")) return e;
        out.data(*r.body.get("data"));
    }
    out.data(Value{}.set("changes", semantic::changes_between(before, tree.snapshot())));
    return std::move(out.result(spec::exit_code::success));
}

}  // namespace

Protocol::Protocol(Runtime& runtime, ProtocolOptions options)
    : runtime_(runtime), tree_(runtime), options_(std::move(options)) {}

void Protocol::ensure_session() {
    if (!options_.remote || options_.session) {
        session_ = options_.session;
        return;
    }
    auto& access = runtime_.agent_access();
    if (session_access_ == &access) return;
    // A host may have constructed the endpoint before AppSpec::prepare.
    // Rebind only when its instance changes, never on cap/grant revocation.
    session_binding_ = runtime_.agent_access_binding();
    session_access_ = &access;
    session_ = access.open_session(options_.session_limits);
}

std::optional<Envelope> Protocol::denied(const Invocation& given, Effect effect) const {
    if (!given.remote) return std::nullopt;
    Invocation call = given;
    call.access = runtime_.agent_access_binding();
    if (clearance(call.operation, effect, false, call) == openas::Decision::refuse)
        return above_cap(std::format("Agent operation `{}`", call.operation), call);
    return std::nullopt;
}

void Protocol::send(const Reply& reply, const Envelope& envelope, const Value& re) const {
    for (const std::string& l : lines(envelope, re)) reply(l);
}

void Protocol::request(std::string_view line, const Reply& reply) {
    mlibs::trace::Span span("request", "agent");
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
    const auto op = text_of(*req, "op");
    if (op == "clock" || op == "pipeline" || op == "profile" || op == "debug") {
        auto by = Actor::parse(text_of(*req, "actor").empty() ? options_.default_actor : text_of(*req, "actor"));
        if (by && session_ && runtime_.agent_access().effective(session_.get()).development_grant) *by = session_->attribute(*by);
        protocol_log.info("Agent control {} by {}: exit={}", op, by ? by->text() : "invalid-actor", e.exit_code().value_or(-1));
    }
    if (!deferred) send(reply, e, re);
}

Envelope Protocol::handle(const Value& req, const Value& re, const Reply& reply, bool& deferred) {
    const std::string_view op = text_of(req, "op");
    Envelope e;
    ensure_session();
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
        call.access = runtime_.agent_access_binding();
        call.session = session_;
        call.operation = std::string(op);
        call.remote = options_.remote;  // over the bus: whatever actor it names (S-10)
        if (const Value* d = req.get("dry_run")) call.dry_run = d->as_bool();  // only what would happen (S-13)
        return call;
    };
    auto current = invocation(req);
    if (!current) return current.error();
    if (const auto* entry = entry_of(op); entry && op != "describe")
        if (auto stopped = denied(*current, operation_effect(*entry, req))) return std::move(*stopped);
    if (op == "describe") {
        Value::List ops;
        for (const auto& entry : catalog) ops.emplace_back(wire_name(entry));
        Value d;
        d.set("interface", interface_name).set("spec", spec::version).set("app", options_.app).set("operations", Value(std::move(ops)));
        d.set("revision", static_cast<std::int64_t>(runtime_.semantic_revision()));
        d.set("capture", static_cast<bool>(options_.capture));
        const auto state = runtime_.agent_access().effective(session_.get());
        Value policy = state.to_value();
        constexpr std::array<std::string_view, 4> mutable_controls{"clock", "pipeline", "profile", "debug"};
        const bool preauthorized_control = state.enabled && state.development_grant && state.cap.allows(openas::Effect::reversible) &&
            std::ranges::any_of(mutable_controls, [&](std::string_view name) { return state.allows(name); });
        policy.set("binding", runtime_.agent_access_binding() ? "instance" : "legacy-global")
            .set("transport_input", options_.input)
            .set("input", options_.input && state.allows("input") && state.cap.allows(openas::Effect::reversible))
            .set("development_controls", preauthorized_control)
            .set("development_controls_meaning", "At least one mutable control is preauthorized by this development session; not all controls. "
                "Published or explicitly granted configurations use per-operation decisions.");
        if (session_) policy.set("session_owner", session_->owner());
        Value::List permissions;
        for (const auto& entry : catalog) {
            const std::string_view name = wire_name(entry);
            Value permission;
            const bool allowed = name == "describe" || (!denied(Invocation{.actor = current->actor, .remote = options_.remote,
                .access = current->access, .session = session_, .operation = std::string(name)},
                operation_effect(entry, Value{})) && (name != "input" || options_.input));
            permission.set("operation", name).set("allowed", allowed);
            if (!allowed) permission.set("reason", !state.session_valid ? "session-invalid" :
                !state.enabled ? "disabled" : name == "input" && !options_.input ? "host-input-disabled" :
                !state.allows(name) ? "restricted-by-layer" : "above-cap");
            permissions.push_back(std::move(permission));
        }
        policy.set("operations", Value(std::move(permissions)));
        d.set("agent_policy", std::move(policy));
        // The application's verbs: what ui_invoke / invoke can name, with what they take.
        Value::List verbs;
        for (const VerbDesc* v : runtime_.verbs().list()) {
            Value args = Value(Value::Map{});
            for (const auto& [n, t] : v->args) args.set(n, t);
            Value one;
            one.set("name", v->name).set("summary", v->summary).set("effect", spec::name(v->effect)).set("args", std::move(args));
            if (!v->answer.empty()) one.set("answer", v->answer);
            if (v->person) one.set("person", true);
            if (v->undoable) one.set("undoable", true);
            const auto decision = runtime_.agent_access().decide(v->name, v->effect, v->person, current->actor, session_.get(), "invoke");
            one.set("agent_decision", decision == openas::Decision::run ? "run" : decision == openas::Decision::ask ? "needs-answer" : "refused");
            verbs.push_back(std::move(one));
        }
        d.set("verbs", Value(std::move(verbs)));
        // What writing for this application takes (W3, D-145): the builders, natives and tokens, from the
        // same source as its TypeScript declarations.
        if (const Value* api = req.get("api"); api && api->as_bool()) d.set("api", api_of(runtime_));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
    if (op == "snapshot") {
        semantic::SnapshotOptions o;
        o.root = std::string(text_of(req, "root"));
        if (const Value* v = req.get("depth")) o.depth = static_cast<int>(v->as_int(-1));
        if (const Value* v = req.get("interactive")) o.interactive = v->as_bool();
        if (const Value* v = req.get("offset")) o.offset = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        if (const Value* v = req.get("limit")) o.limit = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
        // A node's child ids bounded unless asked otherwise (0: all): a list of thirty thousand rows answers with
        // its first 256 and how many there are, the rest paged with `root`.
        o.children_limit = default_children_limit;
        if (const Value* v = req.get("children_limit")) o.children_limit = static_cast<std::size_t>(std::max<std::int64_t>(0, v->as_int()));
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
    if (op == "act") {
        const std::string_view node = text_of(req, "node");
        const std::string_view action = text_of(req, "action");
        if (node.empty() || action.empty())
            return invalid("`act` needs `node` and `action`", R"({"op":"act","node":"main/save","action":"activate","dry_run":false})");
        auto call = invocation(req);
        if (!call) return call.error();
        return with_changes(runtime_, tree_, req, *call, [&] { return tree_.act(node, action, *call); });
    }
    if (op == "invoke") {
        // An application verb (S-1 as for a person's: an irreversible or
        // external one needs its answer).
        const std::string_view verb = text_of(req, "verb");
        if (verb.empty()) return invalid("`invoke` needs a `verb`", R"({"op":"invoke","verb":"notes.new","args":{"title":"Today"}})");
        auto call = invocation(req);
        if (!call) return call.error();
        return with_changes(runtime_, tree_, req, *call, [&] { return runtime_.invoke(verb, *call); });
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
    if (op == "preview") {
        // What the steps would make, shown before it is allowed (Part 2 §4.1, A): the picture now and the
        // picture after, the semantic changes, the steps that would ask the person (shown, not done) —
        // and after it all, the interface as it was.
        const Value* steps = req.get("steps");
        constexpr std::string_view example =
            R"({"op":"preview","steps":[{"node":"main/title","action":"set_value","args":{"value":"Plan"}},{"node":"main/publish","action":"activate"}]})";
        if (!steps || !steps->list() || steps->list()->empty()) return invalid("`preview` needs `steps`: a list of {node, action, args}", example);
        std::vector<semantic::Step> list;
        for (const Value& st : *steps->list()) {
            if (!st.map() || text_of(st, "node").empty() || text_of(st, "action").empty()) return invalid("each step names its `node` and `action`", example);
            list.push_back({std::string(text_of(st, "node")), std::string(text_of(st, "action")), st.get("args") ? *st.get("args") : Value{},
                            std::string(text_of(st, "answer"))});
        }
        auto call = invocation(Value{});
        if (!call) return call.error();
        const Rect region{0, 0, runtime_.size().width, runtime_.size().height};
        const bool pictures = static_cast<bool>(options_.capture) && !(req.get("pictures") && !req.get("pictures")->as_bool());
        auto picture = [&]() -> Value {
            if (!pictures) return Value{};
            auto png = options_.capture(region);
            if (!png) return Value{}.set("error", png.error());
            return Value{}.set("format", "png").set("base64", mlibs::base64::encode(*png));
        };
        Value before = picture();
        Envelope shown = tree_.preview(list, *call, [&] { return picture(); });
        if (const Value* d = shown.data_value()) {
            Value data(Value::Map{});
            if (d->map())
                for (const auto& [key, value] : *d->map())
                    if (key != "seen") data.set(key, value);
            if (pictures) {
                data.set("before", std::move(before));
                if (const Value* seen = d->get("seen")) data.set("after", *seen);
            }
            Envelope out;
            for (const spec::Record& r : shown.records())
                if (r.kind == spec::RecordKind::error && r.body.get("text"))
                    out.error(r.body.get("code") ? std::string(r.body.get("code")->as_string()) : std::string(spec::error_code::failed),
                              std::string(r.body.get("text")->as_string()));
            return out.data(std::move(data)).result(shown.exit_code().value_or(spec::exit_code::success));
        }
        return shown;
    }
    if (op == "record") {
        // A recording a fresh instance replays to the same bytes (W12): every frame since the recorder kept
        // everything, its time and its input, and what it ended as — the tree's hash and the pixels'.
        if (runtime_.record_level() != RecordLevel::full)
            return e.error(spec::error_code::failed, "the recorder keeps every frame only at full (development mode)").result(spec::exit_code::failure);
        if (current->answer == "yes") person_records_ = true;
        if (!person_records_ && !runtime_.agent_access().effective(session_.get()).development_grant)
            return e.error(spec::error_code::needs_answer, "a recording holds what the person did and typed: ask them, then send answer: \"yes\"")
                .result(spec::exit_code::needs_answer);
        Value scenario = runtime_.input_scenario();
        scenario.set("app", options_.app);
        Value expect;
        expect.set("tree", tree_hash(tree_));
        if (options_.capture)
            if (auto png = options_.capture(Rect{0, 0, runtime_.size().width, runtime_.size().height}))
                if (auto hash = pixels_hash(*png)) expect.set("pixels", *hash);
        scenario.set("expect", std::move(expect));
        return e.data(std::move(scenario)).result(spec::exit_code::success);
    }
    if (op == "reload") {
        // Agent hot reload (W2): what a saved file changed, said so that no screenshot need be compared — the
        // semantic changes (the reloader's), where the pixels changed (that region before and after) and the
        // motion it started, sampled over time with the clock held.
        const bool last = req.get("last") && req.get("last")->as_bool();
        const bool pictures = static_cast<bool>(options_.capture) && !last && !(req.get("pictures") && !req.get("pictures")->as_bool());
        const Rect window{0, 0, runtime_.size().width, runtime_.size().height};
        mlibs::image::Image before;
        if (pictures)
            if (auto png = options_.capture(window))
                if (auto decoded = mlibs::image::decode_png(*png)) before = std::move(*decoded);
        auto answer = runtime_.reload_sources(req);
        if (!answer) return e.error(spec::error_code::failed, answer.error()).result(spec::exit_code::failure);
        const Value* reloaded = answer->get("reloaded");
        if (!pictures || before.empty() || !reloaded || !reloaded->list() || reloaded->list()->empty())
            return e.data(std::move(*answer)).result(spec::exit_code::success);
        Reload sampling{.re = re, .reply = reply, .answer = std::move(*answer), .before = before, .previous = std::move(before)};
        if (const Value* every = req.get("every"); every && every->is_number()) sampling.every = std::clamp(every->as_double(), 1.0 / 240, 1.0);
        if (const Value* samples = req.get("samples"); samples && samples->is_number()) sampling.left = std::clamp(static_cast<int>(samples->as_int()), 1, 120);
        sampling.held = !runtime_.time_state().controlled;
        if (sampling.held) runtime_.pause_time(true);
        reloads_.push_back(std::move(sampling));
        deferred = true;
        runtime_.request_frame();
        return e;
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
        waits_.push_back({std::move(*condition), re, deadline, reply, *current});
        runtime_.request_frame();
        return e;
    }
    if (op == "capture") {
        const std::string_view format = text_of(req, "format");
        if (!format.empty() && format != "png" && format != "svg")
            return invalid("`format` is png (pixels, the default) or svg (vectors, the nodes marked)", R"({"op":"capture","format":"svg"})");
        if (format == "svg" && !options_.capture_svg) {
            e.error(spec::error_code::refused, "this host gives no vector picture: capture as png").result(spec::exit_code::failure);
            return e;
        }
        if (format != "svg" && !options_.capture) {
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
        if (format == "svg") {
            auto picture = options_.capture_svg(region);
            if (!picture) {
                e.error(spec::error_code::internal, picture.error()).result(spec::exit_code::failure);
                return e;
            }
            Value::List islands;
            for (const VectorIsland& island : picture->islands) {
                Value one;
                one.set("why", island.why)
                    .set("bounds", Value(Value::List{Value(island.bounds.x), Value(island.bounds.y), Value(island.bounds.width), Value(island.bounds.height)}));
                islands.push_back(std::move(one));
            }
            Value d;
            d.set("format", "svg").set("bounds", Value(Value::List{Value(region.x), Value(region.y), Value(region.width), Value(region.height)}));
            d.set("svg", std::move(picture->svg)).set("islands", Value(std::move(islands)));
            return e.data(std::move(d)).result(spec::exit_code::success);
        }
        auto png = options_.capture(region);
        if (!png) {
            e.error(spec::error_code::internal, png.error()).result(spec::exit_code::failure);
            return e;
        }
        Value d;
        d.set("format", "png").set("bounds", Value(Value::List{Value(region.x), Value(region.y), Value(region.width), Value(region.height)}));
        d.set("base64", mlibs::base64::encode(*png));
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
        deliveries_.push_back({re, reply, std::move(by), std::move(*frames), 0, runtime_.action_journal().size(), {}, *call});
        runtime_.request_frame();
        return e;
    }
    if (op == "intend") {
        // What the agent is about to do, or hands back with (openas §4.4, §5.3): said to the person —
        // its presence shows it — before it acts or as it stops; nothing in the application changes.
        const Value* text = req.get("text");
        if (!text || text->type() != Value::Type::text)
            return invalid("`intend` needs `text`: what it is about to do (or, with hands_back, what it did and what is left); \"\" withdraws it",
                           R"({"op":"intend","text":"Sort the list by date, then remove the duplicates","node":"main/list"})");
        const bool hands_back = req.get("hands_back") && req.get("hands_back")->as_bool();
        runtime_.note({.actor = current->actor.text(), .text = std::string(text->as_string()), .node = std::string(text_of(req, "node")),
                       .hands_back = hands_back});
        return e.data(Value{}.set("said", !text->as_string().empty()).set("hands_back", hands_back)).result(spec::exit_code::success);
    }
    if (op == "timeline" || op == "explain") {
        // What input did (D-148): the interaction recorder's records, and a press explained — the facts
        // of "I clicked and nothing happened", not the person's account of them.
        constexpr std::array levels{"off", "errors", "actions", "interactions", "full"};
        const std::string level = levels[static_cast<std::size_t>(runtime_.record_level())];
        auto record_value = [](const InteractionRecord& r) {
            Value v;
            v.set("cause", static_cast<double>(r.cause)).set("at", r.at).set("kind", r.kind).set("text", r.text).set("actor", r.actor);
            if (!r.node.empty()) v.set("node", r.node);
            if (r.position) v.set("position", Value(Value::List{Value(r.position->x), Value(r.position->y)}));
            return v;
        };
        // A person's input is theirs (D-148, W12): outside development, an agent reads it once the person
        // has said yes (`answer: "yes"`, then for the rest of the session); its own input it always reads.
        if (current->answer == "yes") person_records_ = true;
        const bool may_read_people = person_records_ || runtime_.agent_access().effective(session_.get()).development_grant;
        const auto a_persons = [](const InteractionRecord& r) { return r.actor.starts_with("person:"); };
        constexpr std::string_view ask = "the person's input is theirs to show: ask them, then send the request again with answer: \"yes\"";
        if (op == "timeline") {
            const auto since = static_cast<std::uint64_t>(req.get("since") ? std::max<std::int64_t>(0, req.get("since")->as_int()) : 0);
            const auto limit = static_cast<std::size_t>(req.get("limit") ? std::clamp<std::int64_t>(req.get("limit")->as_int(), 1, 4096) : 200);
            auto records = runtime_.interactions(since);
            if (records.size() > limit) records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(limit));
            const double last = records.empty() ? static_cast<double>(since) : static_cast<double>(records.back().cause);
            std::size_t withheld = 0;
            Value::List list;
            for (const auto& r : records) {
                if (!may_read_people && a_persons(r)) {
                    ++withheld;
                    continue;
                }
                list.push_back(record_value(r));
            }
            Value data = Value{}.set("level", level).set("records", Value(std::move(list))).set("last", last);
            if (!withheld) return e.data(std::move(data)).result(spec::exit_code::success);
            data.set("withheld", static_cast<double>(withheld));
            return e.data(std::move(data))
                .error(spec::error_code::needs_answer, std::format("{} of the person's records withheld: {}", withheld, ask))
                .result(spec::exit_code::needs_answer);
        }
        constexpr std::string_view example = R"({"op":"explain","at":[120,48]})";
        std::optional<Point> at;
        if (const Value* p = req.get("at")) {
            if (!p->list() || p->list()->size() != 2 || !(*p->list())[0].is_number() || !(*p->list())[1].is_number())
                return invalid("`at` is [x, y] in logical pixels", example);
            at = Point{static_cast<float>((*p->list())[0].as_double()), static_cast<float>((*p->list())[1].as_double())};
        }
        const auto cause = static_cast<std::uint64_t>(req.get("cause") ? std::max<std::int64_t>(0, req.get("cause")->as_int()) : 0);
        if (!at && !cause) return invalid("`explain` needs `at` (where the press was) or `cause` (its number, from `timeline`)", example);
        const auto found = runtime_.explain(at, cause);
        if (!found)
            return e.data(Value{}.set("found", false).set("level", level).set("why", level == "off" || level == "errors"
                                                                                      ? "no press is recorded there: the recorder keeps " + level + " (development mode keeps everything)"
                                                                                      : std::string("no press is recorded there")))
                .result(spec::exit_code::success);
        if (!may_read_people && std::ranges::any_of(found->records, a_persons))
            return e.data(Value{}.set("found", true).set("cause", static_cast<double>(found->cause)).set("withheld", true))
                .error(spec::error_code::needs_answer, std::format("a press of the person's: {}", ask))
                .result(spec::exit_code::needs_answer);
        Value::List records, actions;
        for (const auto& r : found->records) records.push_back(record_value(r));
        for (const auto& a : found->actions)
            actions.push_back(Value{}.set("node", a.path).set("action", a.action).set("actor", a.actor).set("exit_code", a.exit_code));
        return e.data(Value{}
                          .set("found", true)
                          .set("cause", static_cast<double>(found->cause))
                          .set("summary", found->summary)
                          .set("records", Value(std::move(records)))
                          .set("actions", Value(std::move(actions))))
            .result(spec::exit_code::success);
    }
    // ── development: inspection, statistics, profiling, logs, debug overlays ──
    if (op == "drew") {
        // What drew a pixel (Part 2 §4.1, B): the node its colour came from, the nodes it sits in, and
        // where in the program each was built — from the picture to the line to change.
        const Value* at = req.get("at");
        constexpr std::string_view example = R"({"op":"drew","at":[120,48]})";
        if (!at || !at->list() || at->list()->size() != 2 || !(*at->list())[0].is_number() || !(*at->list())[1].is_number())
            return invalid("`drew` needs `at`: [x, y] in logical pixels", example);
        if (!options_.pixel_owner) {
            e.error(spec::error_code::refused, "this host cannot say which node drew a pixel (it keeps no object ids)").result(spec::exit_code::failure);
            return e;
        }
        const Point p{static_cast<float>((*at->list())[0].as_double()), static_cast<float>((*at->list())[1].as_double())};
        NodeId n = options_.pixel_owner(p);
        if (!runtime_.valid(n)) {
            return e.data(Value{}.set("at", *at).set("node", Value{}).set("why", "no node drew it: the background")).result(spec::exit_code::success);
        }
        auto source = [&](NodeId node) {
            const std::source_location where = runtime_.origin(node);
            return where.line() == 0 ? Value{} : Value(std::format("{}:{}:{}", where.file_name(), where.line(), where.column()));
        };
        Value::List chain;
        for (NodeId up = n; runtime_.valid(up); up = runtime_.parent(up))
            chain.push_back(Value{}.set("id", runtime_.path(up)).set("source", source(up)));
        Value d;
        d.set("at", *at).set("node", runtime_.path(n)).set("source", source(n)).set("chain", Value(std::move(chain)));
        return e.data(std::move(d)).result(spec::exit_code::success);
    }
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
            mlibs::trace::start();
            return e.data(Value{}.set("recording", true)).result(spec::exit_code::success);
        }
        if (what == "stop") {
            const mlibs::trace::Trace t = mlibs::trace::stop();
            Value::List stats;
            for (const mlibs::trace::Stat& st : t.summary()) {
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
        std::expected<void, std::vector<mlibs::pipeline::Problem>> changed;
        if (action.empty() || action == "show") {
        } else if (action == "replace") {
            changed = runtime_.frame_pipeline().replace(text_of(req, "stage"), text_of(req, "plugin"), config_of(req.get("config")),
                                                         runtime_.stage_plugins());
        } else if (action == "remove") {
            changed = runtime_.frame_pipeline().remove(text_of(req, "stage"), runtime_.stage_plugins());
        } else if (action == "insert") {
            mlibs::pipeline::StageDesc s;
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
            e.error(spec::error_code::invalid_argument, mlibs::pipeline::to_text(changed.error())).result(spec::exit_code::failure);
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
    for (const auto& entry : catalog) known += std::format("{}{}", known.empty() ? "" : ", ", wire_name(entry));
    return invalid(op.empty() ? std::string("a request names its operation in `op`") : std::format("no operation `{}`", op),
                   std::format("operations: {}", known));
}

std::optional<AgentInput> Protocol::take_input() {
    for (auto it = deliveries_.begin(); it != deliveries_.end();) {
        Delivery& d = *it;
        if (d.frames.empty()) { ++it; continue; }
        // Grants, policy or binding may have changed while frames queued.
        // Refuse before dispatch, including text/key events whose component
        // may commit state directly rather than invoke a semantic action.
        if (auto stopped = denied(d.call, Effect::reversible)) {
            send(d.reply, *stopped, d.re);
            const bool began = d.delivered > 0;
            AgentInput cleanup{{PointerEvent{.kind = PointerKind::cancel}}, d.actor, d.call.session};
            it = deliveries_.erase(it);
            // No activation is synthesized. Cancel only releases capture and
            // the transient pressed state from a prior authorized down frame.
            if (began) return cleanup;
            continue;
        }
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
        return AgentInput{std::move(batch), d.actor, d.call.session};
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
    poll_reloads();
    if (waits_.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    std::vector<Wait> still;
    for (Wait& w : waits_) {
        if (auto stopped = denied(w.call, Effect::read)) {
            send(w.reply, *stopped, w.re);
            continue;
        }
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

namespace {

// Where two pictures of a size differ, in their pixels: [x, y, width, height]; none when they do not.
std::optional<std::array<int, 4>> differing(const mlibs::image::Image& one, const mlibs::image::Image& other) {
    if (one.width != other.width || one.height != other.height) return std::array<int, 4>{0, 0, other.width, other.height};
    int left = one.width, top = one.height, right = -1, bottom = -1;
    for (int y = 0; y < one.height; ++y) {
        const auto a = one.row(y), b = other.row(y);
        if (std::ranges::equal(a, b)) continue;
        for (int x = 0; x < one.width; ++x)
            if (!std::ranges::equal(a.subspan(static_cast<std::size_t>(x) * 4, 4), b.subspan(static_cast<std::size_t>(x) * 4, 4))) {
                left = std::min(left, x), right = std::max(right, x);
            }
        top = std::min(top, y), bottom = std::max(bottom, y);
    }
    if (right < 0) return std::nullopt;
    return std::array<int, 4>{left, top, right - left + 1, bottom - top + 1};
}

// A region of a picture as PNG, base64 (what a capture answers).
Value region_png(const mlibs::image::Image& picture, const std::array<int, 4>& region) {
    const auto [x, y, width, height] = region;
    const mlibs::image::View view{.rgba = picture.rgba.data() + (static_cast<std::size_t>(y) * picture.width + x) * 4,
                                  .width = width, .height = height, .stride = picture.width * 4};
    auto png = mlibs::image::encode_png(view);
    if (!png) return Value{}.set("error", png.error());
    return Value{}.set("format", "png").set("base64", mlibs::base64::encode(*png));
}

}  // namespace

std::string tree_hash(semantic::Tree& tree) {
    return std::format("{:016x}", mlibs::hash::Fnv1a{}.text(tree.snapshot().to_value().json()).value());
}

std::expected<std::string, std::string> pixels_hash(std::span<const std::uint8_t> png) {
    auto picture = mlibs::image::decode_png(png);
    if (!picture) return std::unexpected(picture.error());
    mlibs::hash::Fnv1a hash;
    hash.bytes(std::span<const std::uint8_t>(picture->rgba)).byte(0).text(std::format("{}x{}", picture->width, picture->height));
    return std::format("{:016x}", hash.value());
}

void Protocol::poll_reloads() {
    if (reloads_.empty()) return;
    const Rect window{0, 0, runtime_.size().width, runtime_.size().height};
    // Pixels to logical pixels: what a capture is drawn at.
    const auto logical = [&](const mlibs::image::Image& picture, const std::array<int, 4>& region) {
        const double scale = picture.width > 0 ? window.width / picture.width : 1.0;
        Value::List out;
        for (const int value : region) out.emplace_back(std::round(value * scale * 100) / 100);
        return Value(std::move(out));
    };
    std::vector<Reload> still;
    for (Reload& r : reloads_) {
        mlibs::image::Image now;
        if (auto png = options_.capture(window))
            if (auto decoded = mlibs::image::decode_png(*png)) now = std::move(*decoded);
        const auto moved = now.empty() ? std::nullopt : differing(r.previous, now);
        r.samples.push_back(Value{}.set("at", std::round(r.at * 1000) / 1000).set("changed", moved ? logical(now, *moved) : Value{}));
        r.still = moved ? 0 : r.still + 1;
        if (!now.empty()) r.previous = std::move(now);
        if (r.still < 2 && --r.left > 0 && !r.previous.empty()) {
            runtime_.step_time(seconds(r.every));
            r.at += r.every;
            still.push_back(std::move(r));
            continue;
        }
        if (r.held) runtime_.release_time();
        Value pixels;
        const auto changed = differing(r.before, r.previous);
        pixels.set("changed", changed ? logical(r.previous, *changed) : Value{});
        if (changed && changed->at(0) + changed->at(2) <= r.before.width && changed->at(1) + changed->at(3) <= r.before.height) {
            pixels.set("before", region_png(r.before, *changed));
            pixels.set("after", region_png(r.previous, *changed));
        }
        Value data = r.answer;
        data.set("pixels", std::move(pixels));
        data.set("motion", Value{}.set("every", r.every).set("seconds", std::round(r.at * 1000) / 1000).set("settled", r.still >= 2)
                                   .set("samples", Value(std::move(r.samples))));
        Envelope e;
        e.data(std::move(data)).result(spec::exit_code::success);
        send(r.reply, e, r.re);
    }
    reloads_ = std::move(still);
    if (!reloads_.empty()) runtime_.request_frame();
}

std::expected<VectorCapture, std::string> vector_capture(const Runtime& runtime, const xrender::Resources& resources, Rect region, float scale,
                                                        std::optional<Color> background) {
    xrender::svg::Options options;
    options.view = region;
    options.island_scale = scale;
    options.background = background;
    options.node_name = [&runtime](std::uint32_t index) {
        const NodeId node = runtime.node_at_index(index);
        return runtime.valid(node) ? runtime.path(node) : std::string{};
    };
    options.encode_png = [](std::span<const std::uint8_t> rgba, int width, int height) {
        return mlibs::image::encode_png({rgba.data(), width, height, width * 4, mlibs::image::Alpha::premultiplied});
    };
    auto picture = xrender::svg::write(runtime.display_list(), resources, options);
    if (!picture) return std::unexpected(picture.error());
    VectorCapture out{std::move(picture->text), {}};
    for (const auto& island : picture->report.islands) out.islands.push_back({std::string(xrender::svg::describe(island.why)), island.bounds});
    return out;
}

}  // namespace omui::agent
